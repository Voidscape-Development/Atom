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

/// Small, fast, deterministic PRNG. Deterministic seeding matters here: the designer preview and the
/// rendered source should be able to produce the same look from the same settings.
class Random {
public:
	explicit Random(uint32_t seed = 0x9E3779B9u) { setSeed(seed); }

	void setSeed(uint32_t seed) { state_ = seed ? seed : 0x9E3779B9u; }

	uint32_t nextUint()
	{
		// xorshift32
		state_ ^= state_ << 13;
		state_ ^= state_ >> 17;
		state_ ^= state_ << 5;
		return state_;
	}

	/// Uniform in [0, 1).
	float next() { return static_cast<float>(nextUint() >> 8) / 16777216.0f; }

	/// Uniform in [-1, 1].
	float nextSigned() { return next() * 2.0f - 1.0f; }

	float range(float lo, float hi) { return lo + (hi - lo) * next(); }

	int rangeInt(int lo, int hi)
	{
		return hi <= lo ? lo : lo + static_cast<int>(nextUint() % static_cast<uint32_t>(hi - lo + 1));
	}

	bool chance(float probability) { return next() < probability; }

	/// Approximate normal distribution, clamped to +/- 3 sigma-ish. Cheaper than Box-Muller and
	/// good enough for jitter.
	float nextGaussian() { return (next() + next() + next() - 1.5f) * 2.0f; }

	Vec2 insideUnitCircle()
	{
		const float angle = next() * kTwoPi;
		const float radius = std::sqrt(next());
		return {std::cos(angle) * radius, std::sin(angle) * radius};
	}

	Vec2 onUnitCircle()
	{
		const float angle = next() * kTwoPi;
		return {std::cos(angle), std::sin(angle)};
	}

private:
	uint32_t state_ = 0x9E3779B9u;
};

} // namespace atom
