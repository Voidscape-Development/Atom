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

#include <vector>

namespace atom {

/// Draws an AtomSystem with the OBS graphics subsystem.
///
/// One instance per source; the shader and the sprite textures are shared globally. With the
/// post-process bloom mode the atoms are drawn to an offscreen buffer first, so light can bleed
/// between atoms instead of only around each one.
class AtomRenderer {
public:
	~AtomRenderer();

	/// Must be called inside a graphics context, i.e. from the source's video_render.
	void render(const AtomSystem &system);

	/// Releases this renderer's offscreen buffers. Must be called inside a graphics context.
	void releaseBuffers();

	/// Releases the shared effect and sprite cache. Call once on module unload, inside a
	/// graphics context.
	static void releaseGraphics();

private:
	void drawAtoms(const AtomSystem &system);
	void renderLayer(const AtomSystem &system, size_t layerIndex, const std::vector<size_t> &indices);
	void renderTrails(const AtomSystem &system, const AtomLayer &layer, const std::vector<size_t> &indices);

	/// Draws the atoms offscreen, blooms them and composites the result. Returns false if the
	/// buffers could not be created, in which case the caller falls back to drawing directly.
	bool renderWithBloom(const AtomSystem &system, uint32_t width, uint32_t height);
	bool ensureBuffers(uint32_t width, uint32_t height, uint32_t bloomWidth, uint32_t bloomHeight);

	/// Scratch buckets of atom indices per design layer, reused between frames.
	std::vector<std::vector<size_t>> buckets_;

	gs_texrender_t *scene_ = nullptr;
	gs_texrender_t *bloomA_ = nullptr;
	gs_texrender_t *bloomB_ = nullptr;
	uint32_t sceneWidth_ = 0;
	uint32_t sceneHeight_ = 0;
	uint32_t bloomWidth_ = 0;
	uint32_t bloomHeight_ = 0;
};

} // namespace atom
