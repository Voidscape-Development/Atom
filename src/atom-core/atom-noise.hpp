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

#include "atom-math.hpp"

namespace atom {
namespace noise {

inline float hash(int32_t x, int32_t y, int32_t z)
{
	uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
		     static_cast<uint32_t>(z) * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return static_cast<float>(h >> 8) / 16777216.0f;
}

/// Value noise in three dimensions (two spatial plus time). Cheap, tileable enough for drifting
/// turbulence, and identical on every platform so previews match the rendered source.
inline float value3(float x, float y, float z)
{
	const float fx = std::floor(x);
	const float fy = std::floor(y);
	const float fz = std::floor(z);
	const int32_t ix = static_cast<int32_t>(fx);
	const int32_t iy = static_cast<int32_t>(fy);
	const int32_t iz = static_cast<int32_t>(fz);

	const float tx = smoothstep01(x - fx);
	const float ty = smoothstep01(y - fy);
	const float tz = smoothstep01(z - fz);

	const float c000 = hash(ix, iy, iz);
	const float c100 = hash(ix + 1, iy, iz);
	const float c010 = hash(ix, iy + 1, iz);
	const float c110 = hash(ix + 1, iy + 1, iz);
	const float c001 = hash(ix, iy, iz + 1);
	const float c101 = hash(ix + 1, iy, iz + 1);
	const float c011 = hash(ix, iy + 1, iz + 1);
	const float c111 = hash(ix + 1, iy + 1, iz + 1);

	const float x00 = lerp(c000, c100, tx);
	const float x10 = lerp(c010, c110, tx);
	const float x01 = lerp(c001, c101, tx);
	const float x11 = lerp(c011, c111, tx);

	return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

/// Divergence-free-ish 2D flow field derived from two offset noise samples.
inline Vec2 flow(float x, float y, float t)
{
	const float a = value3(x, y, t) * 2.0f - 1.0f;
	const float b = value3(x + 37.7f, y - 19.3f, t + 11.1f) * 2.0f - 1.0f;
	return {a, b};
}

} // namespace noise
} // namespace atom
