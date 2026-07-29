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

#include <array>
#include <cstdint>

namespace atom {

/// Upper bound on recorded trail samples per atom. Trails interpolate between recorded samples, so
/// this caps memory rather than visual length.
constexpr size_t kMaxTrailPoints = 32;

/// One simulated particle.
///
/// Kept deliberately plain: every per-atom value a module might want to vary lives here, so
/// behaviours can be added without touching the pool or the renderer.
struct Atom {
	Vec2 pos;
	Vec2 vel;
	Vec2 spawnPos;

	float age = 0.0f;
	float life = 1.0f;

	/// Base size in pixels before size-over-life shaping.
	float size = 8.0f;
	float rotation = 0.0f;
	float rotationVelocity = 0.0f;

	/// Per-atom colour/alpha multiplier produced by the Offset section.
	Color tint{1.0f, 1.0f, 1.0f, 1.0f};
	/// Per-atom hue rotation in turns, also from the Offset section.
	float hueShift = 0.0f;

	/// Stable per-atom randoms in [0, 1). Modules can use these for anything that must stay
	/// consistent across frames (flicker phase, sprite frame choice, ...).
	float randomA = 0.0f;
	float randomB = 0.0f;
	float randomC = 0.0f;

	/// Index into the design's layer list.
	uint16_t layer = 0;
	/// Set by behaviours (endpoint arrival, collisions, ...) to retire the atom early.
	bool expired = false;

	float normalizedAge() const { return life > 1e-6f ? saturate(age / life) : 1.0f; }
};

/// Ring buffer of past positions for one atom, recorded only when its layer draws a trail.
struct TrailHistory {
	std::array<Vec2, kMaxTrailPoints> points{};
	uint8_t count = 0;
	uint8_t head = 0;
	float sampleTimer = 0.0f;

	void clear()
	{
		count = 0;
		head = 0;
		sampleTimer = 0.0f;
	}

	void push(const Vec2 &p)
	{
		points[head] = p;
		head = static_cast<uint8_t>((head + 1) % kMaxTrailPoints);
		if (count < kMaxTrailPoints)
			++count;
	}

	/// `index` 0 is the most recently recorded sample.
	const Vec2 &at(size_t index) const
	{
		const size_t slot = (head + kMaxTrailPoints - 1 - (index % kMaxTrailPoints)) % kMaxTrailPoints;
		return points[slot];
	}
};

/// Everything the simulation needs from its host each frame.
///
/// The core never talks to OBS: the host resolves things like "where is that other source right
/// now" and hands the answer over through this struct.
struct SimContext {
	/// Emitter surface size in pixels.
	float width = 640.0f;
	float height = 360.0f;

	/// Resolved destination endpoint in emitter-local pixels, when one is configured and available.
	bool hasTarget = false;
	Vec2 target;

	/// Seconds since the source was created; used for time-varying fields such as turbulence.
	float time = 0.0f;

	Vec2 center() const { return {width * 0.5f, height * 0.5f}; }
};

} // namespace atom
