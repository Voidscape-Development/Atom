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
/// One instance per source; the shader and the sprite textures are shared globally.
class AtomRenderer {
public:
	/// Must be called inside a graphics context, i.e. from the source's video_render.
	void render(const AtomSystem &system);

	/// Releases the shared effect. Call once on module unload, inside a graphics context.
	static void releaseGraphics();

private:
	void renderLayer(const AtomSystem &system, size_t layerIndex, const std::vector<size_t> &indices);
	void renderTrails(const AtomSystem &system, const AtomLayer &layer, const std::vector<size_t> &indices);

	/// Scratch buckets of atom indices per design layer, reused between frames.
	std::vector<std::vector<size_t>> buckets_;
};

} // namespace atom
