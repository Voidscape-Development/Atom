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

#pragma once

#include "atom-core/atom-system.hpp"

#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>

#include <vector>

namespace atom {

/// Draws an AtomSystem with the OBS graphics subsystem.
///
/// One instance per source; the shader and the sprite textures are shared globally. With the
/// post-process bloom mode the atoms are drawn to an offscreen buffer first, so light can bleed
/// between atoms instead of only around each one.
class AtomRenderer {
public:
	/// Which layers to draw: every layer, or only those placed behind / in front of a filtered
	/// source.
	enum class Pass { All, Behind, Front };

	~AtomRenderer();

	/// Must be called inside a graphics context, i.e. from the source's video_render. `margin`
	/// widens the drawable area by that many pixels on every side (the atoms keep their own
	/// coordinates); the caller translates by `margin` so the emitter's origin lands inside it.
	void render(const AtomSystem &system, Pass pass = Pass::All, float margin = 0.0f);

	/// Releases everything this renderer holds on the GPU. Must be called inside a graphics
	/// context.
	void releaseBuffers();

	/// Releases the shared effect and sprite cache. Call once on module unload, inside a
	/// graphics context.
	static void releaseGraphics();

private:
	/// Where one layer's three passes live in the shared vertex buffer.
	struct LayerDraw {
		size_t trailStart = 0;
		size_t trailCount = 0;
		size_t glowStart = 0;
		size_t glowCount = 0;
		size_t coreStart = 0;
		size_t coreCount = 0;
	};

	void drawAtoms(const AtomSystem &system, Pass pass);
	void stageLayer(const AtomSystem &system, size_t layerIndex, const std::vector<uint32_t> &indices,
			LayerDraw &draw);
	void stageTrails(const AtomSystem &system, const AtomLayer &layer, const std::vector<uint32_t> &indices);
	void stageQuad(const Vec2 &center, float size, float rotationDegrees, uint32_t color, float u0 = 0.0f,
		       float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);
	void stageSegment(const Vec2 &from, const Vec2 &to, float width, uint32_t color);
	void stageCorners(const Vec2 corners[4], const float uvs[4][2], const uint32_t colors[4]);
	/// Copies the staged vertices to the GPU, growing or shrinking the buffer as needed. Returns
	/// false when there is nothing to draw.
	bool uploadVertices();
	void releaseBloomBuffers();
	/// Draws the atoms offscreen, blooms them and composites the result. Returns false if the
	/// buffers could not be created, in which case the caller falls back to drawing directly.
	bool renderWithBloom(const AtomSystem &system, Pass pass, float margin);
	bool ensureBuffers(uint32_t width, uint32_t height, uint32_t bloomWidth, uint32_t bloomHeight);

	/// Scratch buckets of atom indices per design layer, reused between frames.
	std::vector<std::vector<uint32_t>> buckets_;
	/// Drawable values of the layer currently being staged, evaluated once per atom.
	std::vector<AtomVisual> visuals_;
	std::vector<LayerDraw> draws_;

	/// CPU-side vertex streams for the frame, kept between frames so they stop allocating.
	std::vector<vec3> points_;
	std::vector<vec2> texcoords_;
	std::vector<uint32_t> colors_;

	/// One dynamic buffer, refilled every frame, in place of a buffer created and destroyed for
	/// every draw call.
	gs_vertbuffer_t *vertexBuffer_ = nullptr;
	size_t vertexCapacity_ = 0;
	int underusedFrames_ = 0;

	gs_texrender_t *scene_ = nullptr;
	gs_texrender_t *bloomA_ = nullptr;
	gs_texrender_t *bloomB_ = nullptr;
	uint32_t sceneWidth_ = 0;
	uint32_t sceneHeight_ = 0;
	uint32_t bloomWidth_ = 0;
	uint32_t bloomHeight_ = 0;
};

} // namespace atom
