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

#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <obs-module.h>

#include <cstring>

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

/// Smallest vertex buffer worth allocating; avoids regrowing through tiny sizes on start-up.
constexpr size_t kMinVertexCapacity = 1536;
/// Frames a buffer may sit at under a quarter full before it is traded for a smaller one. The
/// whole buffer is uploaded every frame, so one big burst should not be paid for indefinitely.
constexpr int kShrinkAfterFrames = 240;

void setBlend(bool additive)
{
	// Alpha is accumulated separately so the offscreen bloom buffer ends up premultiplied and
	// composites correctly; drawing straight to the target behaves the same as before.
	gs_blend_function_separate(GS_BLEND_SRCALPHA, additive ? GS_BLEND_ONE : GS_BLEND_INVSRCALPHA, GS_BLEND_ONE,
				   GS_BLEND_INVSRCALPHA);
}

void drawRange(gs_effect_t *effect, gs_eparam_t *image, gs_texture_t *texture, size_t start, size_t count)
{
	// A zero count means "the whole buffer" to gs_draw, so an empty range must not reach it.
	if (!texture || count == 0)
		return;

	gs_effect_set_texture(image, texture);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw(GS_TRIS, static_cast<uint32_t>(start), static_cast<uint32_t>(count));
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
	// Per-source buffers are released by their own sources; this only owns the shared pieces.
	if (g_effect) {
		gs_effect_destroy(g_effect);
		g_effect = nullptr;
	}
	g_effectFailed = false;
	SpriteCache::instance().clear();
}

/// Appends one rotated, colour-modulated quad as two triangles, over a sub-rectangle of the sprite
/// texture (the whole texture unless a sprite sheet is in play).
void AtomRenderer::stageQuad(const Vec2 &center, float size, float rotationDegrees, uint32_t color, float u0, float v0,
			     float u1, float v1)
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
	const float uvs[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
	const uint32_t colors[4] = {color, color, color, color};
	stageCorners(corners, uvs, colors);
}

/// Appends a quad stretched between two points, used for trail segments.
void AtomRenderer::stageSegment(const Vec2 &from, const Vec2 &to, float width, uint32_t color)
{
	const Vec2 along = to - from;
	if (along.lengthSquared() < 1e-6f)
		return;

	const Vec2 direction = along.normalized();
	const Vec2 normal{-direction.y * width * 0.5f, direction.x * width * 0.5f};

	const Vec2 corners[4] = {from - normal, to - normal, to + normal, from + normal};
	// The sprite's horizontal centre line is stretched along the segment, so a soft round sprite
	// becomes a soft streak.
	const float uvs[4][2] = {{0.0f, 0.35f}, {1.0f, 0.35f}, {1.0f, 0.65f}, {0.0f, 0.65f}};
	const uint32_t colors[4] = {color, color, color, color};
	stageCorners(corners, uvs, colors);
}

void AtomRenderer::stageCorners(const Vec2 corners[4], const float uvs[4][2], const uint32_t colors[4])
{
	static const int order[6] = {0, 1, 2, 0, 2, 3};

	const size_t base = points_.size();
	points_.resize(base + 6);
	texcoords_.resize(base + 6);
	colors_.resize(base + 6);

	for (int i = 0; i < 6; ++i) {
		const int index = order[i];
		vec3_set(&points_[base + i], corners[index].x, corners[index].y, 0.0f);
		vec2_set(&texcoords_[base + i], uvs[index][0], uvs[index][1]);
		colors_[base + i] = colors[index];
	}
}

void AtomRenderer::stageTrails(const AtomSystem &system, const AtomLayer &layer, const std::vector<uint32_t> &indices)
{
	if (!system.trailsEnabled())
		return;

	const std::vector<Atom> &atoms = system.atoms();
	const std::vector<TrailHistory> &trails = system.trails();
	const DesignEvaluator &evaluator = system.evaluator();
	const float fade = saturate(layer.trail.fade);

	for (size_t n = 0; n < indices.size(); ++n) {
		const size_t index = indices[n];
		if (index >= trails.size())
			continue;
		const Atom &atom = atoms[index];
		const TrailHistory &history = trails[index];
		if (history.count < 2)
			continue;

		const AtomVisual &visual = visuals_[n];
		const TrailStyle *style = evaluator.trailStyleFor(atom);
		const Color base = layer.trail.inheritColor ? visual.color : layer.trail.tint;

		const size_t samples =
			std::min<size_t>(history.count, static_cast<size_t>(std::max(2, layer.trail.segments)));
		Vec2 previous = atom.pos;
		for (size_t i = 0; i < samples; ++i) {
			const float t = static_cast<float>(i + 1) / static_cast<float>(samples);
			TrailSample modulation;
			if (style)
				modulation = style->sample(t, atom);

			const Vec2 point = history.at(i) + modulation.offset;
			Color color = base;
			color.a *= modulation.alphaScale * lerp(1.0f, 1.0f - t, fade);
			if (color.a <= 0.002f) {
				previous = point;
				continue;
			}

			const float width = std::max(0.5f, visual.size * layer.trail.width * modulation.widthScale);

			if (style && style->discrete())
				stageQuad(point, width, atom.rotation, color.toRGBA());
			else
				stageSegment(previous, point, width, color.toRGBA());
			previous = point;
		}
	}
}

void AtomRenderer::stageLayer(const AtomSystem &system, size_t layerIndex, const std::vector<uint32_t> &indices,
			      LayerDraw &draw)
{
	draw = LayerDraw{};
	if (indices.empty())
		return;

	const AtomLayer &layer = system.config().design.layers[layerIndex];
	const std::vector<Atom> &atoms = system.atoms();
	const DesignEvaluator &evaluator = system.evaluator();

	// Trails, glow and core each need the atom's drawable values. Work them out once and let
	// all three passes read them, rather than evaluating the design three times per atom.
	visuals_.resize(indices.size());
	for (size_t n = 0; n < indices.size(); ++n)
		visuals_[n] = evaluator.evaluate(atoms[indices[n]]);

	draw.trailStart = points_.size();
	if (layer.trail.enabled)
		stageTrails(system, layer, indices);
	draw.trailCount = points_.size() - draw.trailStart;

	// Glow goes down before the core, so the atom itself stays crisp on top of it.
	draw.glowStart = points_.size();
	if (layer.bloom.amount > 1e-3f) {
		for (const AtomVisual &visual : visuals_) {
			if (visual.bloomAlpha <= 0.002f || visual.bloomSize <= 0.0f)
				continue;
			stageQuad(visual.position, visual.bloomSize, 0.0f, visual.bloomColor.toRGBA());
		}
	}
	draw.glowCount = points_.size() - draw.glowStart;

	draw.coreStart = points_.size();
	for (const AtomVisual &visual : visuals_) {
		if (visual.color.a <= 0.002f || visual.size <= 0.0f)
			continue;
		stageQuad(visual.position, visual.size, visual.rotation, visual.color.toRGBA(), visual.u0, visual.v0,
			  visual.u1, visual.v1);
	}
	draw.coreCount = points_.size() - draw.coreStart;
}

bool AtomRenderer::uploadVertices()
{
	const size_t count = points_.size();
	if (count == 0)
		return false;

	// Track how long the buffer has been mostly empty, then let it shrink.
	if (vertexBuffer_ && count < vertexCapacity_ / 4 && vertexCapacity_ > kMinVertexCapacity)
		++underusedFrames_;
	else
		underusedFrames_ = 0;

	if (!vertexBuffer_ || count > vertexCapacity_ || underusedFrames_ > kShrinkAfterFrames) {
		if (vertexBuffer_) {
			gs_vertexbuffer_destroy(vertexBuffer_);
			vertexBuffer_ = nullptr;
		}
		vertexCapacity_ = std::max(kMinVertexCapacity, count + count / 2);
		underusedFrames_ = 0;

		// The buffer takes ownership of this and frees it with bfree().
		gs_vb_data *data = gs_vbdata_create();
		data->num = vertexCapacity_;
		data->points = static_cast<vec3 *>(bzalloc(sizeof(vec3) * vertexCapacity_));
		data->colors = static_cast<uint32_t *>(bzalloc(sizeof(uint32_t) * vertexCapacity_));
		data->num_tex = 1;
		data->tvarray = static_cast<gs_tvertarray *>(bzalloc(sizeof(gs_tvertarray)));
		data->tvarray[0].width = 2;
		data->tvarray[0].array = bzalloc(sizeof(vec2) * vertexCapacity_);

		vertexBuffer_ = gs_vertexbuffer_create(data, GS_DYNAMIC);
		if (!vertexBuffer_) {
			vertexCapacity_ = 0;
			return false;
		}
	}

	gs_vb_data *data = gs_vertexbuffer_get_data(vertexBuffer_);
	if (!data || !data->points || !data->colors || !data->tvarray || !data->tvarray[0].array)
		return false;

	memcpy(data->points, points_.data(), sizeof(vec3) * count);
	memcpy(data->colors, colors_.data(), sizeof(uint32_t) * count);
	memcpy(data->tvarray[0].array, texcoords_.data(), sizeof(vec2) * count);
	gs_vertexbuffer_flush(vertexBuffer_);
	return true;
}

void AtomRenderer::drawAtoms(const AtomSystem &system, Pass pass)
{
	gs_effect_t *effect = atomEffect();
	if (!effect)
		return;

	const AtomDesign &design = system.config().design;
	const std::vector<Atom> &atoms = system.atoms();

	buckets_.resize(design.layers.size());
	for (std::vector<uint32_t> &bucket : buckets_)
		bucket.clear();

	for (size_t i = 0; i < atoms.size(); ++i) {
		const size_t layer = atoms[i].layer;
		if (layer < buckets_.size())
			buckets_[layer].push_back(static_cast<uint32_t>(i));
	}

	// Every layer's geometry goes into one buffer and one upload; the draws below are just
	// ranges of it. Layers still draw in order, each as trails, then glow, then the atoms.
	points_.clear();
	texcoords_.clear();
	colors_.clear();
	draws_.resize(buckets_.size());
	for (size_t i = 0; i < buckets_.size(); ++i) {
		const bool behind = design.layers[i].placementId == "behind";
		if (pass == Pass::All || behind == (pass == Pass::Behind))
			stageLayer(system, i, buckets_[i], draws_[i]);
		else
			draws_[i] = LayerDraw{};
	}

	if (!uploadVertices())
		return;

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_load_vertexbuffer(vertexBuffer_);
	gs_load_indexbuffer(nullptr);

	for (size_t i = 0; i < draws_.size(); ++i) {
		const LayerDraw &draw = draws_[i];
		if (draw.trailCount + draw.glowCount + draw.coreCount == 0)
			continue;

		const AtomLayer &layer = design.layers[i];
		const bool additive = layer.blendId == "additive";
		gs_texture_t *sprite =
			SpriteCache::instance().texture(layer.spriteId, layer.spriteParams, layer.imagePath);

		setBlend(additive);
		drawRange(effect, image, sprite, draw.trailStart, draw.trailCount);

		if (draw.glowCount > 0) {
			gs_texture_t *glow = SpriteCache::instance().texture(
				"soft_circle", bloomSpriteParams(layer.bloom), std::string());
			setBlend(true);
			drawRange(effect, image, glow, draw.glowStart, draw.glowCount);
		}

		setBlend(additive);
		drawRange(effect, image, sprite, draw.coreStart, draw.coreCount);
	}

	gs_load_vertexbuffer(nullptr);
}

AtomRenderer::~AtomRenderer()
{
	// Buffers belong to the graphics subsystem; the source releases them from a graphics
	// context before destroying the renderer.
}

void AtomRenderer::releaseBuffers()
{
	if (vertexBuffer_) {
		gs_vertexbuffer_destroy(vertexBuffer_);
		vertexBuffer_ = nullptr;
	}
	vertexCapacity_ = 0;
	underusedFrames_ = 0;
	releaseBloomBuffers();
}

void AtomRenderer::releaseBloomBuffers()
{
	if (scene_) {
		gs_texrender_destroy(scene_);
		scene_ = nullptr;
	}
	if (bloomA_) {
		gs_texrender_destroy(bloomA_);
		bloomA_ = nullptr;
	}
	if (bloomB_) {
		gs_texrender_destroy(bloomB_);
		bloomB_ = nullptr;
	}
	sceneWidth_ = sceneHeight_ = bloomWidth_ = bloomHeight_ = 0;
}

bool AtomRenderer::ensureBuffers(uint32_t width, uint32_t height, uint32_t bloomWidth, uint32_t bloomHeight)
{
	if (width == 0 || height == 0 || bloomWidth == 0 || bloomHeight == 0)
		return false;

	// gs_texrender objects keep their own size, so they only need recreating when it changes.
	if (!scene_)
		scene_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	if (!bloomA_ || bloomWidth != bloomWidth_ || bloomHeight != bloomHeight_) {
		if (bloomA_)
			gs_texrender_destroy(bloomA_);
		if (bloomB_)
			gs_texrender_destroy(bloomB_);
		bloomA_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		bloomB_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	}

	sceneWidth_ = width;
	sceneHeight_ = height;
	bloomWidth_ = bloomWidth;
	bloomHeight_ = bloomHeight;
	return scene_ && bloomA_ && bloomB_;
}

bool AtomRenderer::renderWithBloom(const AtomSystem &system, Pass pass, float margin)
{
	const uint32_t pad = static_cast<uint32_t>(std::max(0.0f, margin)) * 2;
	const uint32_t width = system.config().emission.width + pad;
	const uint32_t height = system.config().emission.height + pad;

	gs_effect_t *effect = atomEffect();
	if (!effect)
		return false;

	const RenderConfig &render = system.config().render;
	const uint32_t downscale = static_cast<uint32_t>(std::max(1, render.bloomDownscale));
	const uint32_t bloomWidth = std::max(1u, width / downscale);
	const uint32_t bloomHeight = std::max(1u, height / downscale);

	if (!ensureBuffers(width, height, bloomWidth, bloomHeight))
		return false;

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_eparam_t *thresholdParam = gs_effect_get_param_by_name(effect, "threshold");
	gs_eparam_t *intensityParam = gs_effect_get_param_by_name(effect, "intensity");
	gs_eparam_t *texelParam = gs_effect_get_param_by_name(effect, "texel");

	// 1. Atoms into their own buffer. Separate blending for colour and alpha keeps the result
	//    premultiplied, which is what makes the composite below correct over any background.
	gs_texrender_reset(scene_);
	if (gs_texrender_begin(scene_, width, height)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(-margin, static_cast<float>(width) - margin, -margin, static_cast<float>(height) - margin,
			 -100.0f, 100.0f);

		gs_blend_state_push();
		gs_enable_blending(true);
		drawAtoms(system, pass);
		gs_blend_state_pop();

		gs_texrender_end(scene_);
	} else {
		return false;
	}

	gs_texture_t *sceneTexture = gs_texrender_get_texture(scene_);
	if (!sceneTexture)
		return false;

	// 2. Bright pass into the half-size buffer.
	gs_texrender_reset(bloomA_);
	if (gs_texrender_begin(bloomA_, bloomWidth, bloomHeight)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(bloomWidth), 0.0f, static_cast<float>(bloomHeight), -100.0f, 100.0f);

		gs_blend_state_push();
		gs_enable_blending(false);
		gs_effect_set_texture(image, sceneTexture);
		gs_effect_set_float(thresholdParam, saturate(render.bloomThreshold));
		while (gs_effect_loop(effect, "BrightPass"))
			gs_draw_sprite(sceneTexture, 0, bloomWidth, bloomHeight);
		gs_blend_state_pop();

		gs_texrender_end(bloomA_);
	}

	// 3. Separable blur, ping-ponging between the two small buffers.
	const int iterations = std::max(1, std::min(6, render.bloomIterations));
	const float radius = std::max(0.5f, render.bloomRadius) / static_cast<float>(downscale);

	gs_texrender_t *source = bloomA_;
	gs_texrender_t *destination = bloomB_;
	for (int i = 0; i < iterations * 2; ++i) {
		gs_texture_t *input = gs_texrender_get_texture(source);
		if (!input)
			break;

		// Alternate horizontal and vertical, widening the reach on later iterations.
		const float spread = radius * static_cast<float>(1 + i / 2);
		struct vec2 texel;
		if (i % 2 == 0)
			vec2_set(&texel, spread / static_cast<float>(bloomWidth), 0.0f);
		else
			vec2_set(&texel, 0.0f, spread / static_cast<float>(bloomHeight));

		gs_texrender_reset(destination);
		if (gs_texrender_begin(destination, bloomWidth, bloomHeight)) {
			struct vec4 clear;
			vec4_zero(&clear);
			gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
			gs_ortho(0.0f, static_cast<float>(bloomWidth), 0.0f, static_cast<float>(bloomHeight), -100.0f,
				 100.0f);

			gs_blend_state_push();
			gs_enable_blending(false);
			gs_effect_set_texture(image, input);
			gs_effect_set_vec2(texelParam, &texel);
			while (gs_effect_loop(effect, "Blur"))
				gs_draw_sprite(input, 0, bloomWidth, bloomHeight);
			gs_blend_state_pop();

			gs_texrender_end(destination);
		}

		std::swap(source, destination);
	}

	// 4. Composite: the atoms as they were, then the blurred highlights added on top. The
	//    buffers start `margin` up and left of the emitter's origin.
	gs_matrix_push();
	gs_matrix_translate3f(-margin, -margin, 0.0f);
	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_effect_set_texture(image, sceneTexture);
	while (gs_effect_loop(effect, "Copy"))
		gs_draw_sprite(sceneTexture, 0, width, height);

	if (gs_texture_t *bloom = gs_texrender_get_texture(source)) {
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ONE);
		gs_effect_set_texture(image, bloom);
		gs_effect_set_float(intensityParam, std::max(0.0f, render.bloomIntensity));
		while (gs_effect_loop(effect, "Composite"))
			gs_draw_sprite(bloom, 0, width, height);
	}
	gs_blend_state_pop();
	gs_matrix_pop();

	return true;
}

void AtomRenderer::render(const AtomSystem &system, Pass pass, float margin)
{
	if (!atomEffect())
		return;

	const AtomDesign &design = system.config().design;
	const std::vector<Atom> &atoms = system.atoms();
	if (design.layers.empty() || atoms.empty())
		return;

	const RenderConfig &render = system.config().render;
	if (render.usesPostBloom()) {
		if (renderWithBloom(system, pass, margin))
			return;
		// Buffers unavailable: fall through and draw straight to the target.
	} else if (scene_) {
		releaseBloomBuffers();
	}

	gs_blend_state_push();
	gs_enable_blending(true);
	drawAtoms(system, pass);
	gs_blend_state_pop();
}

} // namespace atom
