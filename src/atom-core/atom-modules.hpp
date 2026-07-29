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

#include "atom-config.hpp"
#include "atom-random.hpp"
#include "atom-registry.hpp"
#include "atom-types.hpp"

namespace atom {

/// A position picked by an emitter shape, plus the outward direction at that position.
struct SpawnSample {
	Vec2 position;
	/// Outward-facing unit vector, used by the "shape normal" direction mode. Zero when the shape
	/// has no meaningful normal (a single point, for instance).
	Vec2 normal;
};

/// Decides where atoms are born.
class EmitterShape {
public:
	virtual ~EmitterShape() = default;
	virtual void configure(const ParamBag &params) { (void)params; }
	virtual SpawnSample sample(Random &random, const SimContext &context) const = 0;
};

/// A force or rule applied to every live atom each tick.
class Behavior {
public:
	virtual ~Behavior() = default;
	virtual void configure(const ParamBag &params) { (void)params; }
	virtual void apply(Atom &atom, float dt, const SimContext &context) = 0;
};

/// Procedural sprite mask. Backends rasterize this once into a texture, so a new particle shape
/// needs no shader or asset work.
class SpriteShape {
public:
	virtual ~SpriteShape() = default;
	virtual void configure(const ParamBag &params) { (void)params; }
	/// Coverage in [0, 1] at normalized sprite coordinates, both in [-1, 1].
	virtual float sample(float x, float y) const = 0;
	/// True when the shape wants a host-loaded image instead of a rasterized mask.
	virtual bool usesImage() const { return false; }
};

/// Per-segment modulation of a trail.
struct TrailSample {
	float widthScale = 1.0f;
	float alphaScale = 1.0f;
	Vec2 offset;
};

/// Decides what a trail looks like along its length.
class TrailStyle {
public:
	virtual ~TrailStyle() = default;
	virtual void configure(const ParamBag &params) { (void)params; }
	/// `t` is 0 at the atom and 1 at the tail end.
	virtual TrailSample sample(float t, const Atom &atom) const = 0;
	/// Draw the trail as discrete sprites (sparks) rather than a connected strip.
	virtual bool discrete() const { return false; }
};

/// Alpha over an atom's life.
class FadePath {
public:
	virtual ~FadePath() = default;
	virtual void configure(const ParamBag &params) { (void)params; }
	virtual float alpha(float t, const Atom &atom) const = 0;
};

/// Reshapes normalized age before it is used to sample gradients and curves.
class LifetimeFalloff {
public:
	virtual ~LifetimeFalloff() = default;
	virtual void configure(const ParamBag &params) { (void)params; }
	virtual float shape(float t) const = 0;
};

/// Resolves the destination endpoint, if any.
class EndpointProvider {
public:
	virtual ~EndpointProvider() = default;
	virtual void configure(const ParamBag &params) { (void)params; }
	/// Returns false when there is no target this frame.
	virtual bool resolve(const SimContext &context, const EndpointConfig &config, Vec2 &out) const = 0;
	/// True when the host has to look up an OBS source to fill SimContext::target.
	virtual bool needsHostTarget() const { return false; }
};

using EmitterShapeRegistry = Registry<EmitterShape>;
using BehaviorRegistry = Registry<Behavior>;
using SpriteRegistry = Registry<SpriteShape>;
using TrailRegistry = Registry<TrailStyle>;
using FadePathRegistry = Registry<FadePath>;
using FalloffRegistry = Registry<LifetimeFalloff>;
using EndpointRegistry = Registry<EndpointProvider>;

} // namespace atom
