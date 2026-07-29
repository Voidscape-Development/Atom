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

#include "atom-renderer.hpp"
#include "atom-sprite-cache.hpp"
#include "plugin-support.h"

#include <obs-module.h>

namespace atom {

namespace {

gs_effect_t *g_effect = nullptr;
bool g_effectFailed = false;

gs_effect_t *atomEffect()
{
	if (g_effect || g_effectFailed)
		return g_effect;

	char *path = obs_module_file("effects/atom.effect");
	if (!path) {
		obs_log(LOG_ERROR, "effects/atom.effect is missing from the plugin data directory");
		g_effectFailed = true;
		return nullptr;
	}

	char *errors = nullptr;
	g_effect = gs_effect_create_from_file(path, &errors);
	if (!g_effect) {
		obs_log(LOG_ERROR, "failed to compile atom.effect: %s", errors ? errors : "unknown error");
		g_effectFailed = true;
	}

	bfree(errors);
	bfree(path);
	return g_effect;
}

/// Emits one rotated, colour-modulated quad as two triangles.
void emitQuad(const Vec2 &center, float size, float rotationDegrees, uint32_t color)
{
	const float half = size * 0.5f;
	const float radians = deg2rad(rotationDegrees);
	const float c = std::cos(radians);
	const float s = std::sin(radians);

	const Vec2 right{c * half, s * half};
	const Vec2 down{-s * half, c * half};

	const Vec2 corners[4] = {
		center - right - down,
		center + right - down,
		center + right + down,
		center - right + down,
	};
	const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
	const int order[6] = {0, 1, 2, 0, 2, 3};

	for (int i = 0; i < 6; ++i) {
		const int index = order[i];
		gs_color(color);
		gs_texcoord(uvs[index][0], uvs[index][1], 0);
		gs_vertex2f(corners[index].x, corners[index].y);
	}
}

/// Emits a quad stretched between two points, used for trail segments.
void emitSegment(const Vec2 &from, const Vec2 &to, float width, uint32_t colorFrom, uint32_t colorTo)
{
	const Vec2 along = to - from;
	if (along.lengthSquared() < 1e-6f)
		return;

	const Vec2 direction = along.normalized();
	const Vec2 normal{-direction.y * width * 0.5f, direction.x * width * 0.5f};

	const Vec2 corners[4] = {from - normal, to - normal, to + normal, from + normal};
	const uint32_t colors[4] = {colorFrom, colorTo, colorTo, colorFrom};
	// The sprite's horizontal centre line is stretched along the segment, so a soft round sprite
	// becomes a soft streak.
	const float uvs[4][2] = {{0.0f, 0.35f}, {1.0f, 0.35f}, {1.0f, 0.65f}, {0.0f, 0.65f}};
	const int order[6] = {0, 1, 2, 0, 2, 3};

	for (int i = 0; i < 6; ++i) {
		const int index = order[i];
		gs_color(colors[index]);
		gs_texcoord(uvs[index][0], uvs[index][1], 0);
		gs_vertex2f(corners[index].x, corners[index].y);
	}
}

void setBlend(bool additive)
{
	gs_blend_function(GS_BLEND_SRCALPHA, additive ? GS_BLEND_ONE : GS_BLEND_INVSRCALPHA);
}

void drawWithTexture(gs_effect_t *effect, gs_texture_t *texture, const std::function<void()> &emit)
{
	if (!texture)
		return;

	gs_eparam_t *param = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture(param, texture);

	while (gs_effect_loop(effect, "Draw")) {
		gs_render_start(true);
		emit();
		gs_render_stop(GS_TRIS);
	}
}

/// Bloom uses its own soft sprite so a star or a square still glows like a light source.
ParamBag bloomSpriteParams(const BloomConfig &bloom)
{
	ParamBag params;
	params.set("falloff", static_cast<double>(lerp(3.2f, 1.1f, saturate(bloom.softness))));
	return params;
}

} // namespace

void AtomRenderer::releaseGraphics()
{
	if (g_effect) {
		gs_effect_destroy(g_effect);
		g_effect = nullptr;
	}
	g_effectFailed = false;
	SpriteCache::instance().clear();
}

void AtomRenderer::renderTrails(const AtomSystem &system, const AtomLayer &layer, const std::vector<size_t> &indices)
{
	gs_effect_t *effect = atomEffect();
	if (!effect || !system.trailsEnabled())
		return;

	const std::vector<Atom> &atoms = system.atoms();
	const std::vector<TrailHistory> &trails = system.trails();
	const DesignEvaluator &evaluator = system.evaluator();

	gs_texture_t *texture = SpriteCache::instance().texture(layer.spriteId, layer.spriteParams, layer.imagePath);
	if (!texture)
		return;

	setBlend(layer.blendId == "additive");

	drawWithTexture(effect, texture, [&] {
		for (const size_t index : indices) {
			if (index >= trails.size())
				continue;

			const Atom &atom = atoms[index];
			const TrailHistory &history = trails[index];
			if (history.count < 2)
				continue;

			const AtomVisual visual = evaluator.evaluate(atom);
			const TrailStyle *style = evaluator.trailStyleFor(atom);
			const Color base = layer.trail.inheritColor ? visual.color : layer.trail.tint;
			const float fade = saturate(layer.trail.fade);

			const size_t samples =
				std::min<size_t>(history.count, static_cast<size_t>(std::max(2, layer.trail.segments)));

			Vec2 previous = atom.pos;
			for (size_t i = 0; i < samples; ++i) {
				const float t = static_cast<float>(i + 1) / static_cast<float>(samples);
				TrailSample modulation;
				if (style)
					modulation = style->sample(t, atom);

				Color color = base;
				color.a *= modulation.alphaScale * lerp(1.0f, 1.0f - t, fade);
				if (color.a <= 0.002f) {
					previous = history.at(i) + modulation.offset;
					continue;
				}

				const Vec2 point = history.at(i) + modulation.offset;
				const float width =
					std::max(0.5f, visual.size * layer.trail.width * modulation.widthScale);

				if (style && style->discrete())
					emitQuad(point, width, atom.rotation, color.toRGBA());
				else
					emitSegment(previous, point, width, color.toRGBA(), color.toRGBA());

				previous = point;
			}
		}
	});
}

void AtomRenderer::renderLayer(const AtomSystem &system, size_t layerIndex, const std::vector<size_t> &indices)
{
	if (indices.empty())
		return;

	gs_effect_t *effect = atomEffect();
	if (!effect)
		return;

	const AtomDesign &design = system.config().design;
	if (layerIndex >= design.layers.size())
		return;

	const AtomLayer &layer = design.layers[layerIndex];
	const std::vector<Atom> &atoms = system.atoms();
	const DesignEvaluator &evaluator = system.evaluator();

	if (layer.trail.enabled)
		renderTrails(system, layer, indices);

	// Glow first, so the atom core stays crisp on top of it.
	if (layer.bloom.amount > 1e-3f) {
		gs_texture_t *glow =
			SpriteCache::instance().texture("soft_circle", bloomSpriteParams(layer.bloom), std::string());
		setBlend(true);
		drawWithTexture(effect, glow, [&] {
			for (const size_t index : indices) {
				const AtomVisual visual = evaluator.evaluate(atoms[index]);
				if (visual.bloomAlpha <= 0.002f || visual.bloomSize <= 0.0f)
					continue;
				emitQuad(visual.position, visual.bloomSize, 0.0f, visual.bloomColor.toRGBA());
			}
		});
	}

	gs_texture_t *texture = SpriteCache::instance().texture(layer.spriteId, layer.spriteParams, layer.imagePath);
	setBlend(layer.blendId == "additive");
	drawWithTexture(effect, texture, [&] {
		for (const size_t index : indices) {
			const AtomVisual visual = evaluator.evaluate(atoms[index]);
			if (visual.color.a <= 0.002f || visual.size <= 0.0f)
				continue;
			emitQuad(visual.position, visual.size, visual.rotation, visual.color.toRGBA());
		}
	});
}

void AtomRenderer::render(const AtomSystem &system)
{
	if (!atomEffect())
		return;

	const AtomDesign &design = system.config().design;
	const std::vector<Atom> &atoms = system.atoms();
	if (design.layers.empty() || atoms.empty())
		return;

	buckets_.resize(design.layers.size());
	for (std::vector<size_t> &bucket : buckets_)
		bucket.clear();

	for (size_t i = 0; i < atoms.size(); ++i) {
		const size_t layer = atoms[i].layer;
		if (layer < buckets_.size())
			buckets_[layer].push_back(i);
	}

	gs_blend_state_push();
	gs_enable_blending(true);

	for (size_t i = 0; i < buckets_.size(); ++i)
		renderLayer(system, i, buckets_[i]);

	gs_blend_state_pop();
}

} // namespace atom
