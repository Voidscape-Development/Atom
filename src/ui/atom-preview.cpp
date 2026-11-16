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
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

#include <obs-module.h>

#include <map>
#include <vector>

namespace atom {
namespace ui {

namespace {

constexpr uint32_t kMaskResolution = 64;
/// How faint atoms of the kinds that are not being edited are drawn.
constexpr float kDimmedAlpha = 0.18f;
/// Points sampled from the emitter shape to draw its outline.
constexpr int kShapeSamples = 260;

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
		// resize() keeps the allocation from the previous frame; every texel is written below.
		rgb.resize(static_cast<size_t>(w) * h * 3);
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

/// The masks one layer draws with, looked up once per frame. Building a cache key means
/// formatting strings, which is far too slow to do for every atom.
struct LayerMasks {
	const std::vector<uint8_t> *sprite = nullptr;
	const std::vector<uint8_t> *glow = nullptr;
	bool additive = true;
};

void drawTrail(Canvas &canvas, const AtomSystem &system, size_t index, const AtomLayer &layer, const LayerMasks &masks,
	       const AtomVisual &visual, float scale, const Vec2 &offset)
{
	const std::vector<TrailHistory> &trails = system.trails();
	if (index >= trails.size())
		return;

	const TrailHistory &history = trails[index];
	if (history.count < 2)
		return;

	const Atom &atom = system.atoms()[index];
	const TrailStyle *style = system.evaluator().trailStyleFor(atom);
	const std::vector<uint8_t> &mask = *masks.sprite;
	const Color base = layer.trail.inheritColor ? visual.color : layer.trail.tint;
	const bool additive = masks.additive;

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

QImage renderAtomFrame(const AtomSystem &system, const QSize &size, const QColor &background, int highlightLayer)
{
	// Only ever called from the UI thread, so one canvas can be reused for every frame.
	static Canvas canvas;
	canvas.reset(std::max(1, size.width()), std::max(1, size.height()), background);

	// Fit the emitter surface into the widget, letterboxing to keep the aspect ratio honest.
	const float scale = std::min(static_cast<float>(size.width()) / std::max(1.0f, system.width()),
				     static_cast<float>(size.height()) / std::max(1.0f, system.height()));
	const Vec2 offset{(static_cast<float>(size.width()) - system.width() * scale) * 0.5f,
			  (static_cast<float>(size.height()) - system.height() * scale) * 0.5f};

	const AtomDesign &design = system.config().design;
	const DesignEvaluator &evaluator = system.evaluator();
	const std::vector<Atom> &atoms = system.atoms();

	static std::vector<LayerMasks> layerMasks;
	layerMasks.resize(design.layers.size());
	for (size_t i = 0; i < design.layers.size(); ++i) {
		const AtomLayer &layer = design.layers[i];
		layerMasks[i].sprite = &maskFor(layer.spriteId, layer.spriteParams);
		layerMasks[i].glow = &maskFor("soft_circle", bloomParams(layer.bloom));
		layerMasks[i].additive = layer.blendId == "additive";
	}

	for (size_t i = 0; i < atoms.size(); ++i) {
		const Atom &atom = atoms[i];
		if (atom.layer >= design.layers.size())
			continue;

		const AtomLayer &layer = design.layers[atom.layer];
		const LayerMasks &masks = layerMasks[atom.layer];
		AtomVisual visual = evaluator.evaluate(atom);
		if (highlightLayer >= 0 && static_cast<int>(atom.layer) != highlightLayer) {
			visual.color.a *= kDimmedAlpha;
			visual.bloomAlpha *= kDimmedAlpha;
			visual.bloomColor.a *= kDimmedAlpha;
		}
		if (visual.color.a <= 0.002f && visual.bloomAlpha <= 0.002f)
			continue;

		if (layer.trail.enabled && system.trailsEnabled())
			drawTrail(canvas, system, i, layer, masks, visual, scale, offset);

		if (visual.bloomAlpha > 0.002f) {
			drawSprite(canvas, *masks.glow, visual.position * scale + offset, visual.bloomSize * scale,
				   0.0f, visual.bloomColor, true);
		}

		drawSprite(canvas, *masks.sprite, visual.position * scale + offset, visual.size * scale,
			   visual.rotation, visual.color, masks.additive);
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
	timer_.setInterval(16);
	lastTick_ = QDateTime::currentMSecsSinceEpoch();
}

AtomPreview::~AtomPreview() = default;

QSize AtomPreview::sizeHint() const
{
	return QSize(420, 240);
}

void AtomPreview::setConfig(const EmitterConfig &config)
{
	base_ = config;
	resampleShape();
	modulation_.setRoutes(config.modulation);
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

	const float dt = std::min(elapsed, 0.05f);
	time_ += dt;

	ModContext modContext;
	modContext.time = time_;
	modContext.dt = dt;
	system_->updateValues([&](EmitterConfig &live) { return modulation_.applyInPlace(base_, modContext, live); });

	SimContext context;
	system_->update(dt, context);
	update();
}

void AtomPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	// The timer only runs while there is something on screen to animate.
	lastTick_ = QDateTime::currentMSecsSinceEpoch();
	timer_.start();
}

void AtomPreview::hideEvent(QHideEvent *event)
{
	timer_.stop();
	QWidget::hideEvent(event);
}

void AtomPreview::setOverlay(Overlay overlay)
{
	overlay_ = overlay;
	setCursor(overlay == Overlay::Launch || overlay == Overlay::Target ? Qt::CrossCursor : Qt::ArrowCursor);
	update();
}

void AtomPreview::setHighlightLayer(int layer)
{
	highlightLayer_ = layer;
	update();
}

void AtomPreview::setChecker(bool checker)
{
	checker_ = checker;
	update();
}

void AtomPreview::burst(int count)
{
	system_->burst(count);
	update();
}

void AtomPreview::resampleShape()
{
	shapeSamples_.clear();
	shapeCenter_ =
		Vec2{static_cast<float>(base_.emission.width) * 0.5f, static_cast<float>(base_.emission.height) * 0.5f};

	// The shape module itself says where atoms can appear, so any registered shape gets an
	// outline here without the preview knowing what it is.
	std::unique_ptr<EmitterShape> shape = EmitterShapeRegistry::instance().create(base_.emission.shapeId);
	if (!shape)
		return;
	ParamBag params = base_.emission.shapeParams;
	params.applyDefaults(EmitterShapeRegistry::instance().schemaFor(base_.emission.shapeId));
	shape->configure(params);

	SimContext context;
	context.width = static_cast<float>(base_.emission.width);
	context.height = static_cast<float>(base_.emission.height);

	Random random(0x5EEDu);
	Vec2 sum;
	shapeSamples_.reserve(kShapeSamples);
	for (int i = 0; i < kShapeSamples; ++i) {
		const Vec2 point = shape->sample(random, context).position;
		shapeSamples_.push_back(point);
		sum += point;
	}
	shapeCenter_ = sum * (1.0f / static_cast<float>(kShapeSamples));
}

void AtomPreview::mapping(float &scale, QPointF &offset) const
{
	// Must match renderAtomFrame(), which letterboxes the emitter surface into the widget.
	const float w = std::max(1.0f, system_->width());
	const float h = std::max(1.0f, system_->height());
	scale = std::min(static_cast<float>(width()) / w, static_cast<float>(height()) / h);
	offset = QPointF((static_cast<float>(width()) - w * scale) * 0.5f,
			 (static_cast<float>(height()) - h * scale) * 0.5f);
}

void AtomPreview::mousePressEvent(QMouseEvent *event)
{
	mouseMoveEvent(event);
}

void AtomPreview::mouseMoveEvent(QMouseEvent *event)
{
	if (!(event->buttons() & Qt::LeftButton))
		return;
	if (overlay_ != Overlay::Launch && overlay_ != Overlay::Target)
		return;

	float scale = 1.0f;
	QPointF offset;
	mapping(scale, offset);
	if (scale <= 0.0f)
		return;
	emit dragged((event->position() - offset) / scale);
}

void AtomPreview::paintOverlay(QPainter &painter)
{
	if (overlay_ == Overlay::None)
		return;

	float scale = 1.0f;
	QPointF offset;
	mapping(scale, offset);
	const auto toWidget = [&](const Vec2 &p) {
		return QPointF(p.x * scale, p.y * scale) + offset;
	};

	const QColor accent = palette().color(QPalette::Highlight);
	painter.setRenderHint(QPainter::Antialiasing, true);

	// The emitter shape is context for every overlay: faint behind an arrow, bright on its own.
	QColor dot = accent.lighter(150);
	dot.setAlpha(overlay_ == Overlay::Shape ? 230 : 90);
	painter.setPen(Qt::NoPen);
	painter.setBrush(dot);
	for (const Vec2 &sample : shapeSamples_)
		painter.drawEllipse(toWidget(sample), 1.6, 1.6);

	if (overlay_ == Overlay::Shape) {
		// The emitter surface itself, so its size on the canvas is visible too.
		QColor frame = accent.lighter(150);
		frame.setAlpha(120);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(frame, 1.0, Qt::DashLine));
		painter.drawRect(
			QRectF(toWidget(Vec2{0.0f, 0.0f}), toWidget(Vec2{system_->width(), system_->height()})));
		return;
	}

	if (overlay_ == Overlay::Launch) {
		if (base_.emission.directionModeId != "angle")
			return;

		const QPointF origin = toWidget(shapeCenter_);
		const float length = std::max(60.0f, std::min(width(), height()) * 0.3f);
		const float angle = base_.physics.emitAngle;
		const float half = std::min(180.0f, base_.physics.emitSpread * 0.5f);

		// Spread, as a fan around the launch direction.
		QColor fan = accent;
		fan.setAlpha(60);
		painter.setPen(QPen(accent.lighter(150), 1.0, Qt::DashLine));
		painter.setBrush(fan);
		const QRectF arc(origin.x() - length, origin.y() - length, length * 2.0f, length * 2.0f);
		// Qt measures pie angles counter-clockwise from three o'clock, in sixteenths of a degree,
		// while emitter angles run clockwise because y points down.
		painter.drawPie(arc, static_cast<int>(-(angle + half) * 16.0f), static_cast<int>(half * 2.0f * 16.0f));

		const Vec2 direction = angleToVector(angle);
		const QPointF tip = origin + QPointF(direction.x, direction.y) * length;
		painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(origin, tip);
		const Vec2 left = angleToVector(angle + 150.0f);
		const Vec2 right = angleToVector(angle - 150.0f);
		painter.drawLine(tip, tip + QPointF(left.x, left.y) * 12.0);
		painter.drawLine(tip, tip + QPointF(right.x, right.y) * 12.0);
		return;
	}

	if (overlay_ == Overlay::Target) {
		if (base_.physics.endpoint.modeId != "point")
			return;
		const QPointF target = toWidget(Vec2{base_.physics.endpoint.point.x * system_->width(),
						     base_.physics.endpoint.point.y * system_->height()});
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(Qt::white, 2.0));
		painter.drawEllipse(target, 9.0, 9.0);
		painter.drawLine(target - QPointF(15.0, 0.0), target + QPointF(15.0, 0.0));
		painter.drawLine(target - QPointF(0.0, 15.0), target + QPointF(0.0, 15.0));
	}
}

void AtomPreview::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.drawImage(0, 0, renderAtomFrame(*system_, size(), background_, highlightLayer_));

	if (checker_) {
		// Atoms were drawn over a flat backdrop; lay a faint grid on top so see-through areas
		// read as see-through without a second render path.
		QColor tile(255, 255, 255, 18);
		const int cell = 16;
		for (int y = 0; y < height(); y += cell) {
			for (int x = (y / cell) % 2 ? cell : 0; x < width(); x += cell * 2)
				painter.fillRect(x, y, cell, cell, tile);
		}
	}

	paintOverlay(painter);

	painter.setPen(QColor(200, 200, 210, 160));
	painter.drawText(rect().adjusted(8, 6, -8, -6), Qt::AlignTop | Qt::AlignRight,
			 QString::fromUtf8(obs_module_text("Atom.Designer.AtomCount")).arg(system_->count()));
}

} // namespace ui
} // namespace atom
