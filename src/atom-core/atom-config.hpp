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

#include "atom-fields.hpp"
#include "atom-value.hpp"

#include <string>
#include <vector>

namespace atom {

/// Names of the module registries. Used by ParamSpec::enumRegistry and by generic UI code.
namespace registries {
constexpr const char *kEmitterShape = "emitter_shape";
constexpr const char *kBehavior = "behavior";
constexpr const char *kSprite = "sprite";
constexpr const char *kTrail = "trail";
constexpr const char *kFadePath = "fade_path";
constexpr const char *kFalloff = "falloff";
constexpr const char *kEndpoint = "endpoint";
} // namespace registries

/// A configured instance of a registered behaviour. The extras list on PhysicsConfig holds these,
/// which is how new forces reach the simulation without changing any struct.
struct BehaviorInstance {
	std::string id;
	bool enabled = true;
	ParamBag params;
};

/// Where and how often atoms are born.
struct EmissionConfig {
	uint32_t width = 640;
	uint32_t height = 360;

	/// Registered emitter shape ("point", "box", "edge", "center", "shape", ...).
	std::string shapeId = "box";
	/// Parameters of the selected shape. Schema comes from the shape's ModuleInfo.
	ParamBag shapeParams;

	/// "continuous", "burst" or "both".
	std::string modeId = "continuous";
	float rate = 80.0f;
	int burstCount = 40;
	float burstInterval = 1.5f;

	int maxAtoms = 4000;
	bool prewarm = true;
	bool randomSeed = true;
	int seed = 1;

	/// How the initial travel direction is chosen: "angle", "shape_normal", "outward", "inward",
	/// "random" or "endpoint".
	std::string directionModeId = "angle";
};

/// Destination endpoint an atom is drawn towards, if any.
struct EndpointConfig {
	/// Registered endpoint provider ("none", "point", "source", ...).
	std::string modeId = "none";
	/// Target point in normalized emitter coordinates (0..1), used by the "point" provider.
	Vec2 point{0.5f, 0.5f};
	/// Name of the OBS source to track, used by the "source" provider. Resolved by the host.
	std::string sourceName;
	/// Which part of the tracked source to aim at.
	std::string anchorId = "center";

	/// "attract" (steady pull), "arrive" (guaranteed arrival by end of life) or "orbit".
	std::string arrivalId = "attract";
	float strength = 1.0f;
	float arriveRadius = 10.0f;
	bool killOnArrival = true;
	/// Per-atom randomization of the target position, in pixels.
	float scatter = 0.0f;
};

/// Per-atom variation applied at spawn. Zero everywhere means every atom emits identically.
struct OffsetConfig {
	float position = 0.0f;
	float angle = 0.0f;
	float speed = 0.15f;
	float size = 0.25f;
	float lifetime = 0.2f;
	float rotation = 1.0f;
	float hue = 0.0f;
	float brightness = 0.0f;
	float alpha = 0.0f;
	/// Fraction of atoms that start part-way through their life, so a fresh emitter looks settled.
	float spawnPhase = 0.0f;
};

/// How atoms move and how long they live.
struct PhysicsConfig {
	/// Pixels per second squared. Negative values make atoms rise like embers or smoke.
	float gravity = 140.0f;
	/// Direction gravity pulls in, in degrees (0 = right, 90 = down).
	float gravityAngle = 90.0f;

	float initialSpeed = 90.0f;
	/// Direction atoms are launched in, in degrees. Used when directionModeId is "angle".
	float emitAngle = -90.0f;
	/// Cone width around the emit direction, in degrees.
	float emitSpread = 25.0f;

	float drag = 0.4f;
	float turbulence = 0.0f;
	float turbulenceScale = 180.0f;
	float turbulenceSpeed = 0.6f;

	float lifetime = 2.0f;
	/// Registered falloff shaping the atom's normalized age ("linear", "ease_in", "ease_out", ...).
	std::string falloffId = "linear";
	/// Parameters of the selected falloff. Schema comes from the falloff's ModuleInfo.
	ParamBag falloffParams;
	Curve falloffCurve = Curve::linear(0.0f, 1.0f);

	float spin = 0.0f;
	bool alignToVelocity = false;

	EndpointConfig endpoint;
	OffsetConfig offset;

	/// Additional registered behaviours, applied after the built-in forces.
	std::vector<BehaviorInstance> extras;
};

/// Colour of an atom over its life.
struct ColorConfig {
	/// "solid", "gradient" (over lifetime) or "random_stop" (one gradient stop picked at spawn).
	std::string modeId = "gradient";
	Color color{1.0f, 0.85f, 0.4f, 1.0f};
	Gradient gradient{{{0.0f, Color(1.0f, 0.95f, 0.65f, 1.0f)},
			   {0.45f, Color(1.0f, 0.5f, 0.12f, 0.9f)},
			   {1.0f, Color(0.35f, 0.06f, 0.0f, 0.0f)}}};
};

/// Glow around an atom, from a soft puff of smoke to a hard light source.
struct BloomConfig {
	float amount = 0.4f;
	/// Glow radius as a multiple of the atom's size.
	float radius = 2.2f;
	/// 0 = tight light source, 1 = diffuse puff.
	float softness = 0.5f;
	bool inheritColor = true;
	Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};

/// Atom size and how it changes over life.
struct SizeConfig {
	float base = 12.0f;
	float minimum = 0.0f;
	/// Multiplier applied to `base`, sampled with the atom's normalized age.
	Curve overLife = Curve::constant(1.0f);
};

/// Trail left behind an atom.
struct TrailConfig {
	bool enabled = false;
	/// Registered trail style ("streak", "sparkle", "ribbon", ...).
	std::string styleId = "streak";
	ParamBag styleParams;
	/// Seconds of position history to keep.
	float length = 0.25f;
	/// Trail width as a fraction of the atom's size.
	float width = 0.6f;
	float fade = 1.0f;
	int segments = 12;
	bool inheritColor = true;
	Color tint{1.0f, 1.0f, 1.0f, 1.0f};
};

/// Alpha over life: plain fade-outs through to firework/flare style blowouts.
struct FadeConfig {
	/// Registered fade path ("linear_out", "ease_out", "flare", "blink", "custom", ...).
	std::string pathId = "ease_out";
	/// Parameters of the selected fade path. Schema comes from the path's ModuleInfo.
	ParamBag pathParams;
	Curve customCurve = Curve::linear(1.0f, 0.0f);
	/// Fraction of life spent fading in.
	float fadeIn = 0.05f;
	float flicker = 0.0f;
	float flickerSpeed = 9.0f;
};

/// One kind of atom in a design. A design can hold several, each with its own spawn weight, which
/// is how a single emitter produces e.g. sparks plus smoke.
struct AtomLayer {
	std::string id;
	std::string name = "Atom";
	bool enabled = true;
	float weight = 1.0f;

	/// Registered sprite shape ("soft_circle", "hard_circle", "spark", "star", "ring", "image").
	std::string spriteId = "soft_circle";
	ParamBag spriteParams;
	std::string imagePath;
	/// "normal" or "additive".
	std::string blendId = "additive";

	ColorConfig color;
	BloomConfig bloom;
	SizeConfig size;
	TrailConfig trail;
	FadeConfig fade;
};

/// The look of an emitter: everything edited in the Atom Designer window.
struct AtomDesign {
	std::string name = "Untitled";
	/// Id of the preset this design was created from, if any.
	std::string presetId;
	std::vector<AtomLayer> layers;

	static AtomDesign defaultDesign();

	AtomLayer *findLayer(const std::string &id);
	const AtomLayer *findLayer(const std::string &id) const;
	/// Picks a layer index using the layers' weights. `roll` is in [0, 1).
	size_t pickLayer(float roll) const;
	bool anyTrails() const;
};

/// Full configuration of one Atom Emitter source.
struct EmitterConfig {
	EmissionConfig emission;
	PhysicsConfig physics;
	AtomDesign design;
};

/// Field tables. These describe the config structs to the rest of the plugin; nothing else should
/// enumerate their members.
const FieldTable<EmissionConfig> &emissionFields();
const FieldTable<PhysicsConfig> &physicsFields();
const FieldTable<AtomLayer> &layerFields();

/// Generates a reasonably unique layer id.
std::string makeLayerId();

} // namespace atom
