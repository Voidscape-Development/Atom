/*
Atom - Particle system plugin for OBS Studio
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "atom-preview.hpp"
#include "obs/atom-sprite-cache.hpp"

#include <QDateTime>
#include <QPainter>

#include <map>
#include <vector>

namespace atom {
namespace ui {

namespace {

constexpr uint32_t kMaskResolution = 64;

/// CPU-side sprite masks, keyed exactly like the GPU sprite cache.
const std::vector<uint8_t> &maskFor(const std::string &spriteId, const ParamBag &params)
{
	static std::map<std::string, std::vector<uint8_t>> cache;

	const std::string key = spriteCacheKey(spriteId, params, std::string());
	const auto it = cache.find(key);
	if (it != cache.end())
		return it->second;

	return cache.emplace(key, SpriteCache::rasterize(spriteId, params, kMaskResolution)).first->second;
}

/// Float RGB accumulation buffer. Blending in linear-ish float keeps additive stacks from
/// clipping the way 8-bit accumulation would.
struct Canvas {
	int width = 0;
	int height = 0;
	std::vector<float> rgb;

	void reset(int w, int h, const QColor &background)
	{
		width = w;
		height = h;
		rgb.assign(static_cast<size_t>(w) * h * 3, 0.0f);
		for (size_t i = 0; i < rgb.size(); i += 3) {
			rgb[i + 0] = static_cast<float>(background.redF());
			rgb[i + 1] = static_cast<float>(background.greenF());
			rgb[i + 2] = static_cast<float>(background.blueF());
		}
	}

	QImage toImage() const
	{
		QImage image(width, height, QImage::Format_RGB32);
		for (int y = 0; y < height; ++y) {
			QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
			for (int x = 0; x < width; ++x) {
				const size_t index = (static_cast<size_t>(y) * width + x) * 3;
				line[x] = qRgb(static_cast<int>(saturate(rgb[index + 0]) * 255.0f),
					       static_cast<int>(saturate(rgb[index + 1]) * 255.0f),
					       static_cast<int>(saturate(rgb[index + 2]) * 255.0f));
			}
		}
		return image;
	}
};

void drawSprite(Canvas &canvas, const std::vector<uint8_t> &mask, const Vec2 &center, float size, float rotation,
		const Color &color, bool additive)
{
	if (size <= 0.5f || color.a <= 0.002f || mask.empty())
		return;

	const float half = size * 0.5f;
	const int minX = std::max(0, static_cast<int>(std::floor(center.x - half)));
	const int maxX = std::min(canvas.width - 1, static_cast<int>(std::ceil(center.x + half)));
	const int minY = std::max(0, static_cast<int>(std::floor(center.y - half)));
	const int maxY = std::min(canvas.height - 1, static_cast<int>(std::ceil(center.y + half)));
	if (minX > maxX || minY > maxY)
		return;

	const float radians = deg2rad(-rotation);
	const float c = std::cos(radians);
	const float s = std::sin(radians);

	for (int y = minY; y <= maxY; ++y) {
		for (int x = minX; x <= maxX; ++x) {
			const float dx = (static_cast<float>(x) + 0.5f - center.x) / half;
			const float dy = (static_cast<float>(y) + 0.5f - center.y) / half;

			const float u = dx * c - dy * s;
			const float v = dx * s + dy * c;
			if (u < -1.0f || u > 1.0f || v < -1.0f || v > 1.0f)
				continue;

			const int mx = std::min<int>(kMaskResolution - 1,
						     static_cast<int>((u * 0.5f + 0.5f) * kMaskResolution));
			const int my = std::min<int>(kMaskResolution - 1,
						     static_cast<int>((v * 0.5f + 0.5f) * kMaskResolution));
			const float coverage =
				static_cast<float>(mask[static_cast<size_t>(my) * kMaskResolution + mx]) / 255.0f;
			if (coverage <= 0.003f)
				continue;

			const float alpha = coverage * color.a;
			const size_t index = (static_cast<size_t>(y) * canvas.width + x) * 3;
			if (additive) {
				canvas.rgb[index + 0] += color.r * alpha;
				canvas.rgb[index + 1] += color.g * alpha;
				canvas.rgb[index + 2] += color.b * alpha;
			} else {
				canvas.rgb[index + 0] = lerp(canvas.rgb[index + 0], color.r, alpha);
				canvas.rgb[index + 1] = lerp(canvas.rgb[index + 1], color.g, alpha);
				canvas.rgb[index + 2] = lerp(canvas.rgb[index + 2], color.b, alpha);
			}
		}
	}
}

ParamBag bloomParams(const BloomConfig &bloom)
{
	ParamBag params;
	params.set("falloff", static_cast<double>(lerp(3.2f, 1.1f, saturate(bloom.softness))));
	return params;
}

void drawTrail(Canvas &canvas, const AtomSystem &system, size_t index, const AtomLayer &layer, const AtomVisual &visual,
	       float scale, const Vec2 &offset)
{
	const std::vector<TrailHistory> &trails = system.trails();
	if (index >= trails.size())
		return;

	const TrailHistory &history = trails[index];
	if (history.count < 2)
		return;

	const Atom &atom = system.atoms()[index];
	const TrailStyle *style = system.evaluator().trailStyleFor(atom);
	const std::vector<uint8_t> &mask = maskFor(layer.spriteId, layer.spriteParams);
	const Color base = layer.trail.inheritColor ? visual.color : layer.trail.tint;
	const bool additive = layer.blendId == "additive";

	const size_t samples = std::min<size_t>(history.count, static_cast<size_t>(std::max(2, layer.trail.segments)));
	Vec2 previous = atom.pos;

	for (size_t i = 0; i < samples; ++i) {
		const float t = static_cast<float>(i + 1) / static_cast<float>(samples);
		TrailSample modulation;
		if (style)
			modulation = style->sample(t, atom);

		Color color = base;
		color.a *= modulation.alphaScale * lerp(1.0f, 1.0f - t, saturate(layer.trail.fade));

		const Vec2 point = history.at(i) + modulation.offset;
		const float width = std::max(1.0f, visual.size * layer.trail.width * modulation.widthScale);

		if (style && style->discrete()) {
			drawSprite(canvas, mask, point * scale + offset, width * scale, atom.rotation, color, additive);
		} else {
			// Dabs along the segment; a preview does not need the GPU path's stretched quads.
			const float distance = (point - previous).length();
			const int steps = std::max(1, static_cast<int>(distance / std::max(1.0f, width * 0.35f)));
			for (int step = 0; step <= steps; ++step) {
				const Vec2 p =
					lerp(previous, point, static_cast<float>(step) / static_cast<float>(steps));
				drawSprite(canvas, mask, p * scale + offset, width * scale, 0.0f, color, additive);
			}
		}

		previous = point;
	}
}

} // namespace

QImage renderAtomFrame(const AtomSystem &system, const QSize &size, const QColor &background)
{
	Canvas canvas;
	canvas.reset(std::max(1, size.width()), std::max(1, size.height()), background);

	// Fit the emitter surface into the widget, letterboxing to keep the aspect ratio honest.
	const float scale = std::min(static_cast<float>(size.width()) / std::max(1.0f, system.width()),
				     static_cast<float>(size.height()) / std::max(1.0f, system.height()));
	const Vec2 offset{(static_cast<float>(size.width()) - system.width() * scale) * 0.5f,
			  (static_cast<float>(size.height()) - system.height() * scale) * 0.5f};

	const AtomDesign &design = system.config().design;
	const DesignEvaluator &evaluator = system.evaluator();
	const std::vector<Atom> &atoms = system.atoms();

	for (size_t i = 0; i < atoms.size(); ++i) {
		const Atom &atom = atoms[i];
		if (atom.layer >= design.layers.size())
			continue;

		const AtomLayer &layer = design.layers[atom.layer];
		const AtomVisual visual = evaluator.evaluate(atom);
		if (visual.color.a <= 0.002f && visual.bloomAlpha <= 0.002f)
			continue;

		if (layer.trail.enabled && system.trailsEnabled())
			drawTrail(canvas, system, i, layer, visual, scale, offset);

		if (visual.bloomAlpha > 0.002f) {
			drawSprite(canvas, maskFor("soft_circle", bloomParams(layer.bloom)),
				   visual.position * scale + offset, visual.bloomSize * scale, 0.0f, visual.bloomColor,
				   true);
		}

		drawSprite(canvas, maskFor(layer.spriteId, layer.spriteParams), visual.position * scale + offset,
			   visual.size * scale, visual.rotation, visual.color, layer.blendId == "additive");
	}

	return canvas.toImage();
}

QImage renderPresetThumbnail(const EmitterConfig &config, const QSize &size, float warmup)
{
	AtomSystem system;
	system.configure(config);

	SimContext context;
	const float step = 1.0f / 60.0f;
	const int steps = std::max(1, static_cast<int>(warmup / step));
	for (int i = 0; i < steps; ++i)
		system.update(step, context);

	return renderAtomFrame(system, size, QColor(24, 24, 28));
}

AtomPreview::AtomPreview(QWidget *parent) : QWidget(parent)
{
	setMinimumSize(240, 140);
	system_ = std::make_unique<AtomSystem>();

	connect(&timer_, &QTimer::timeout, this, &AtomPreview::tick);
	timer_.start(16);
	lastTick_ = QDateTime::currentMSecsSinceEpoch();
}

AtomPreview::~AtomPreview() = default;

QSize AtomPreview::sizeHint() const
{
	return QSize(420, 240);
}

void AtomPreview::setConfig(const EmitterConfig &config)
{
	system_->configure(config);
	update();
}

void AtomPreview::restart()
{
	system_->reset();
	update();
}

void AtomPreview::setPlaying(bool playing)
{
	playing_ = playing;
	lastTick_ = QDateTime::currentMSecsSinceEpoch();
}

void AtomPreview::setBackground(const QColor &color)
{
	background_ = color;
	update();
}

void AtomPreview::tick()
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const float elapsed = static_cast<float>(now - lastTick_) / 1000.0f;
	lastTick_ = now;

	if (!playing_ || !isVisible())
		return;

	SimContext context;
	system_->update(std::min(elapsed, 0.05f), context);
	update();
}

void AtomPreview::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.drawImage(0, 0, renderAtomFrame(*system_, size(), background_));

	painter.setPen(QColor(200, 200, 210, 160));
	painter.drawText(rect().adjusted(6, 4, -6, -4), Qt::AlignTop | Qt::AlignRight,
			 QString("%1 atoms").arg(system_->count()));
}

} // namespace ui
} // namespace atom
