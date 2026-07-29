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

#include "atom-core/atom-modules.hpp"

#include <graphics/graphics.h>

#include <map>
#include <string>
#include <vector>

namespace atom {

/// Rasterizes procedural SpriteShape masks into GPU textures and keeps them around.
///
/// Sprites are pure functions of their parameters, so one texture per (sprite, parameters) pair is
/// shared by every source using it.
class SpriteCache {
public:
	static SpriteCache &instance();

	/// Must be called inside a graphics context. Returns nullptr if the sprite cannot be built.
	gs_texture_t *texture(const std::string &spriteId, const ParamBag &params, const std::string &imagePath);

	/// Rasterizes a mask to CPU memory as 8-bit coverage; used by the designer preview.
	static std::vector<uint8_t> rasterize(const std::string &spriteId, const ParamBag &params, uint32_t size);

	/// Frees every cached texture. Must be called inside a graphics context.
	void clear();

	size_t size() const { return textures_.size(); }

private:
	SpriteCache() = default;
	~SpriteCache() = default;

	std::map<std::string, gs_texture_t *> textures_;
};

/// Stable cache key for a sprite plus its parameters.
std::string spriteCacheKey(const std::string &spriteId, const ParamBag &params, const std::string &imagePath);

} // namespace atom
