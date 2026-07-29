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

#include "atom-presets.hpp"

namespace atom {

namespace {

AtomLayer makeLayer(const char *name, const char *spriteId, const Gradient &gradient, float size)
{
	AtomLayer layer;
	layer.id = makeLayerId();
	layer.name = name;
	layer.spriteId = spriteId;
	layer.color.modeId = "gradient";
	layer.color.gradient = gradient;
	layer.size.base = size;
	return layer;
}

Color rgba(int r, int g, int b, int a = 255)
{
	return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

AtomPreset embers()
{
	AtomPreset preset;
	preset.id = "embers";
	preset.name = "Embers";
	preset.category = "fire";
	preset.description = "Atom.Preset.Embers.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "box";
	config.emission.shapeParams.set("origin", Vec2{0.0f, 0.75f});
	config.emission.shapeParams.set("size", Vec2{1.0f, 0.25f});
	config.emission.rate = 70.0f;
	config.emission.directionModeId = "angle";

	config.physics.gravity = -70.0f;
	config.physics.gravityAngle = 90.0f;
	config.physics.initialSpeed = 45.0f;
	config.physics.emitAngle = -90.0f;
	config.physics.emitSpread = 40.0f;
	config.physics.drag = 0.6f;
	config.physics.turbulence = 55.0f;
	config.physics.turbulenceScale = 160.0f;
	config.physics.lifetime = 2.6f;
	config.physics.offset.size = 0.5f;
	config.physics.offset.lifetime = 0.35f;
	config.physics.offset.brightness = 0.25f;

	AtomLayer ember = makeLayer(
		"Ember", "soft_circle",
		Gradient{{{0.0f, rgba(255, 244, 200)}, {0.35f, rgba(255, 140, 30, 235)}, {1.0f, rgba(90, 12, 0, 0)}}},
		7.0f);
	ember.bloom.amount = 0.65f;
	ember.bloom.radius = 3.0f;
	ember.bloom.softness = 0.35f;
	ember.fade.pathId = "ease_out";
	ember.fade.flicker = 0.35f;
	ember.size.overLife = Curve{{{0.0f, 0.4f}, {0.2f, 1.0f}, {1.0f, 0.3f}}};
	config.design.layers.push_back(ember);

	config.design.name = preset.name;
	return preset;
}

AtomPreset fire()
{
	AtomPreset preset;
	preset.id = "fire";
	preset.name = "Fire";
	preset.category = "fire";
	preset.description = "Atom.Preset.Fire.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "line";
	config.emission.shapeParams.set("from", Vec2{0.25f, 0.9f});
	config.emission.shapeParams.set("to", Vec2{0.75f, 0.9f});
	config.emission.rate = 160.0f;

	config.physics.gravity = -180.0f;
	config.physics.initialSpeed = 60.0f;
	config.physics.emitAngle = -90.0f;
	config.physics.emitSpread = 22.0f;
	config.physics.drag = 1.4f;
	config.physics.turbulence = 90.0f;
	config.physics.turbulenceScale = 110.0f;
	config.physics.turbulenceSpeed = 1.1f;
	config.physics.lifetime = 1.3f;
	config.physics.offset.size = 0.45f;
	config.physics.offset.position = 6.0f;

	AtomLayer flame = makeLayer("Flame", "smoke",
				    Gradient{{{0.0f, rgba(255, 255, 220)},
					      {0.25f, rgba(255, 190, 60, 240)},
					      {0.6f, rgba(220, 70, 20, 170)},
					      {1.0f, rgba(40, 10, 5, 0)}}},
				    34.0f);
	flame.bloom.amount = 0.5f;
	flame.bloom.radius = 2.0f;
	flame.bloom.softness = 0.8f;
	flame.size.overLife = Curve{{{0.0f, 0.5f}, {0.35f, 1.0f}, {1.0f, 0.7f}}};
	flame.fade.pathId = "ease_out";
	config.design.layers.push_back(flame);

	AtomLayer spark = makeLayer("Spark", "spark",
				    Gradient{{{0.0f, rgba(255, 250, 210)}, {1.0f, rgba(255, 120, 20, 0)}}}, 5.0f);
	spark.weight = 0.25f;
	spark.bloom.amount = 0.8f;
	spark.bloom.radius = 3.5f;
	spark.trail.enabled = true;
	spark.trail.styleId = "comet";
	spark.trail.length = 0.16f;
	spark.trail.width = 0.5f;
	config.design.layers.push_back(spark);

	config.design.name = preset.name;
	return preset;
}

AtomPreset sparks()
{
	AtomPreset preset;
	preset.id = "sparks";
	preset.name = "Sparks";
	preset.category = "fire";
	preset.description = "Atom.Preset.Sparks.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "point";
	config.emission.shapeParams.set("position", Vec2{0.5f, 0.35f});
	config.emission.modeId = "both";
	config.emission.rate = 25.0f;
	config.emission.burstCount = 45;
	config.emission.burstInterval = 1.8f;
	config.emission.directionModeId = "random";

	config.physics.gravity = 420.0f;
	config.physics.initialSpeed = 260.0f;
	config.physics.emitSpread = 360.0f;
	config.physics.drag = 1.1f;
	config.physics.lifetime = 1.1f;
	config.physics.alignToVelocity = true;
	config.physics.offset.speed = 0.55f;
	config.physics.offset.lifetime = 0.4f;

	AtomLayer spark = makeLayer(
		"Spark", "soft_circle",
		Gradient{{{0.0f, rgba(255, 255, 235)}, {0.4f, rgba(255, 200, 90, 255)}, {1.0f, rgba(255, 90, 10, 0)}}},
		4.5f);
	spark.bloom.amount = 0.9f;
	spark.bloom.radius = 3.2f;
	spark.bloom.softness = 0.15f;
	spark.trail.enabled = true;
	spark.trail.styleId = "streak";
	spark.trail.length = 0.14f;
	spark.trail.width = 0.7f;
	spark.trail.segments = 10;
	spark.fade.pathId = "hold";
	spark.fade.pathParams.set("hold", 0.55);
	config.design.layers.push_back(spark);

	config.design.name = preset.name;
	return preset;
}

AtomPreset flare()
{
	AtomPreset preset;
	preset.id = "flare";
	preset.name = "Firework Flare";
	preset.category = "fire";
	preset.description = "Atom.Preset.Flare.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "circle";
	config.emission.shapeParams.set("radius", 0.05);
	config.emission.modeId = "burst";
	config.emission.burstCount = 160;
	config.emission.burstInterval = 2.4f;
	config.emission.directionModeId = "outward";

	config.physics.gravity = 90.0f;
	config.physics.initialSpeed = 320.0f;
	config.physics.emitSpread = 12.0f;
	config.physics.drag = 1.6f;
	config.physics.lifetime = 2.2f;
	config.physics.offset.speed = 0.3f;
	config.physics.offset.hue = 0.25f;

	AtomLayer shell = makeLayer(
		"Shell", "soft_circle",
		Gradient{{{0.0f, rgba(255, 255, 255)}, {0.3f, rgba(120, 200, 255)}, {1.0f, rgba(40, 60, 255, 0)}}},
		6.0f);
	shell.bloom.amount = 0.85f;
	shell.bloom.radius = 3.6f;
	shell.bloom.softness = 0.2f;
	shell.fade.pathId = "flare";
	shell.trail.enabled = true;
	shell.trail.styleId = "sparkle";
	shell.trail.length = 0.35f;
	shell.trail.width = 0.5f;
	config.design.layers.push_back(shell);

	config.design.name = preset.name;
	return preset;
}

AtomPreset smoke()
{
	AtomPreset preset;
	preset.id = "smoke";
	preset.name = "Smoke Puff";
	preset.category = "smoke";
	preset.description = "Atom.Preset.Smoke.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "center";
	config.emission.shapeParams.set("radius", 18.0);
	config.emission.rate = 22.0f;

	config.physics.gravity = -40.0f;
	config.physics.initialSpeed = 26.0f;
	config.physics.emitAngle = -90.0f;
	config.physics.emitSpread = 55.0f;
	config.physics.drag = 0.9f;
	config.physics.turbulence = 30.0f;
	config.physics.turbulenceScale = 240.0f;
	config.physics.lifetime = 4.5f;
	config.physics.spin = 12.0f;
	config.physics.offset.rotation = 1.0f;
	config.physics.offset.size = 0.5f;

	AtomLayer puff = makeLayer("Puff", "smoke",
				   Gradient{{{0.0f, rgba(190, 190, 200, 0)},
					     {0.2f, rgba(150, 150, 160, 150)},
					     {1.0f, rgba(90, 90, 100, 0)}}},
				   70.0f);
	puff.blendId = "normal";
	puff.bloom.amount = 0.0f;
	puff.size.overLife = Curve{{{0.0f, 0.35f}, {1.0f, 1.6f}}};
	puff.fade.pathId = "ease_out";
	puff.fade.fadeIn = 0.18f;
	config.design.layers.push_back(puff);

	config.design.name = preset.name;
	return preset;
}

AtomPreset fog()
{
	AtomPreset preset;
	preset.id = "fog";
	preset.name = "Fog Drift";
	preset.category = "smoke";
	preset.description = "Atom.Preset.Fog.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "edge";
	config.emission.shapeParams.set("edges", std::string("left"));
	config.emission.rate = 12.0f;

	config.physics.gravity = 0.0f;
	config.physics.initialSpeed = 35.0f;
	config.physics.emitAngle = 0.0f;
	config.physics.emitSpread = 12.0f;
	config.physics.drag = 0.2f;
	config.physics.turbulence = 12.0f;
	config.physics.turbulenceScale = 400.0f;
	config.physics.lifetime = 12.0f;
	config.physics.offset.size = 0.4f;
	config.physics.offset.spawnPhase = 1.0f;

	AtomLayer bank = makeLayer("Bank", "smoke",
				   Gradient{{{0.0f, rgba(200, 205, 220, 0)},
					     {0.25f, rgba(190, 195, 215, 90)},
					     {1.0f, rgba(170, 175, 200, 0)}}},
				   180.0f);
	bank.blendId = "normal";
	bank.bloom.amount = 0.0f;
	bank.fade.fadeIn = 0.25f;
	bank.fade.pathId = "hold";
	bank.fade.pathParams.set("hold", 0.55);
	config.design.layers.push_back(bank);

	config.design.name = preset.name;
	return preset;
}

AtomPreset magicDust()
{
	AtomPreset preset;
	preset.id = "magic_dust";
	preset.name = "Magic Dust";
	preset.category = "magic";
	preset.description = "Atom.Preset.MagicDust.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "box";
	config.emission.rate = 60.0f;
	config.emission.directionModeId = "random";

	config.physics.gravity = -25.0f;
	config.physics.initialSpeed = 30.0f;
	config.physics.emitSpread = 360.0f;
	config.physics.drag = 1.2f;
	config.physics.turbulence = 40.0f;
	config.physics.lifetime = 3.0f;
	config.physics.offset.hue = 0.35f;
	config.physics.offset.size = 0.6f;
	config.physics.offset.spawnPhase = 1.0f;

	AtomLayer dust = makeLayer("Dust", "star",
				   Gradient{{{0.0f, rgba(255, 255, 255, 0)},
					     {0.25f, rgba(180, 220, 255)},
					     {0.7f, rgba(200, 140, 255, 220)},
					     {1.0f, rgba(120, 90, 255, 0)}}},
				   9.0f);
	dust.bloom.amount = 0.7f;
	dust.bloom.radius = 3.0f;
	dust.fade.pathId = "pulse";
	dust.trail.enabled = true;
	dust.trail.styleId = "sparkle";
	dust.trail.length = 0.4f;
	dust.trail.width = 0.35f;
	config.design.layers.push_back(dust);

	config.design.name = preset.name;
	return preset;
}

AtomPreset runeCircle()
{
	AtomPreset preset;
	preset.id = "rune_circle";
	preset.name = "Rune Circle";
	preset.category = "magic";
	preset.description = "Atom.Preset.RuneCircle.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "circle";
	config.emission.shapeParams.set("radius", 0.75);
	config.emission.shapeParams.set("inner_radius", 0.7);
	config.emission.rate = 90.0f;
	config.emission.directionModeId = "shape_normal";

	config.physics.gravity = 0.0f;
	config.physics.initialSpeed = 10.0f;
	config.physics.emitSpread = 20.0f;
	config.physics.drag = 0.8f;
	config.physics.lifetime = 2.4f;
	config.physics.endpoint.modeId = "point";
	config.physics.endpoint.arrivalId = "orbit";
	config.physics.endpoint.strength = 0.35f;
	config.physics.endpoint.killOnArrival = false;
	config.physics.offset.spawnPhase = 1.0f;

	AtomLayer rune = makeLayer(
		"Rune", "ring",
		Gradient{{{0.0f, rgba(120, 255, 220, 0)}, {0.3f, rgba(120, 255, 220)}, {1.0f, rgba(30, 120, 255, 0)}}},
		12.0f);
	rune.bloom.amount = 0.6f;
	rune.bloom.radius = 2.6f;
	config.design.layers.push_back(rune);

	config.design.name = preset.name;
	return preset;
}

AtomPreset confetti()
{
	AtomPreset preset;
	preset.id = "confetti";
	preset.name = "Confetti";
	preset.category = "celebration";
	preset.description = "Atom.Preset.Confetti.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "edge";
	config.emission.shapeParams.set("edges", std::string("top"));
	config.emission.modeId = "both";
	config.emission.rate = 40.0f;
	config.emission.burstCount = 120;
	config.emission.burstInterval = 3.0f;

	config.physics.gravity = 220.0f;
	config.physics.initialSpeed = 60.0f;
	config.physics.emitAngle = 90.0f;
	config.physics.emitSpread = 70.0f;
	config.physics.drag = 1.8f;
	config.physics.turbulence = 60.0f;
	config.physics.lifetime = 4.0f;
	config.physics.spin = 220.0f;
	config.physics.offset.rotation = 1.0f;
	config.physics.offset.spawnPhase = 0.5f;

	AtomLayer paper = makeLayer("Paper", "square",
				    Gradient{{{0.0f, rgba(255, 80, 120)},
					      {0.33f, rgba(255, 210, 60)},
					      {0.66f, rgba(90, 220, 140)},
					      {1.0f, rgba(90, 160, 255)}}},
				    11.0f);
	paper.blendId = "normal";
	paper.color.modeId = "random_stop";
	paper.bloom.amount = 0.0f;
	paper.fade.pathId = "hold";
	paper.fade.pathParams.set("hold", 0.8);
	config.design.layers.push_back(paper);

	config.design.name = preset.name;
	return preset;
}

AtomPreset bubbles()
{
	AtomPreset preset;
	preset.id = "bubbles";
	preset.name = "Bubbles";
	preset.category = "celebration";
	preset.description = "Atom.Preset.Bubbles.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "edge";
	config.emission.shapeParams.set("edges", std::string("bottom"));
	config.emission.rate = 18.0f;

	config.physics.gravity = -60.0f;
	config.physics.initialSpeed = 20.0f;
	config.physics.emitAngle = -90.0f;
	config.physics.emitSpread = 25.0f;
	config.physics.drag = 0.9f;
	config.physics.turbulence = 25.0f;
	config.physics.lifetime = 6.0f;
	config.physics.offset.size = 0.6f;
	config.physics.offset.spawnPhase = 1.0f;

	AtomLayer bubble = makeLayer("Bubble", "ring",
				     Gradient{{{0.0f, rgba(180, 230, 255, 0)},
					       {0.2f, rgba(200, 240, 255, 190)},
					       {1.0f, rgba(160, 220, 255, 0)}}},
				     26.0f);
	bubble.blendId = "normal";
	bubble.bloom.amount = 0.25f;
	bubble.bloom.radius = 1.6f;
	bubble.spriteParams.set("thickness", 0.22);
	bubble.spriteParams.set("softness", 0.7);
	config.design.layers.push_back(bubble);

	config.design.name = preset.name;
	return preset;
}

AtomPreset snow()
{
	AtomPreset preset;
	preset.id = "snow";
	preset.name = "Snow";
	preset.category = "weather";
	preset.description = "Atom.Preset.Snow.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "edge";
	config.emission.shapeParams.set("edges", std::string("top"));
	config.emission.shapeParams.set("inset", -30.0);
	config.emission.rate = 55.0f;

	config.physics.gravity = 45.0f;
	config.physics.initialSpeed = 20.0f;
	config.physics.emitAngle = 90.0f;
	config.physics.emitSpread = 25.0f;
	config.physics.drag = 1.6f;
	config.physics.turbulence = 45.0f;
	config.physics.turbulenceScale = 260.0f;
	config.physics.turbulenceSpeed = 0.35f;
	config.physics.lifetime = 9.0f;
	config.physics.offset.size = 0.55f;
	config.physics.offset.spawnPhase = 1.0f;

	AtomLayer flake = makeLayer("Flake", "soft_circle",
				    Gradient{{{0.0f, rgba(255, 255, 255, 0)},
					      {0.1f, rgba(255, 255, 255, 235)},
					      {0.9f, rgba(235, 245, 255, 220)},
					      {1.0f, rgba(220, 235, 255, 0)}}},
				    6.0f);
	flake.blendId = "normal";
	flake.bloom.amount = 0.15f;
	config.design.layers.push_back(flake);

	config.design.name = preset.name;
	return preset;
}

AtomPreset rain()
{
	AtomPreset preset;
	preset.id = "rain";
	preset.name = "Rain";
	preset.category = "weather";
	preset.description = "Atom.Preset.Rain.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "edge";
	config.emission.shapeParams.set("edges", std::string("top"));
	config.emission.shapeParams.set("inset", -60.0);
	config.emission.rate = 220.0f;

	config.physics.gravity = 900.0f;
	config.physics.gravityAngle = 100.0f;
	config.physics.initialSpeed = 420.0f;
	config.physics.emitAngle = 100.0f;
	config.physics.emitSpread = 3.0f;
	config.physics.drag = 0.0f;
	config.physics.lifetime = 1.6f;
	config.physics.alignToVelocity = true;
	config.physics.offset.speed = 0.2f;

	AtomLayer drop = makeLayer("Drop", "soft_circle",
				   Gradient{{{0.0f, rgba(190, 215, 255, 200)}, {1.0f, rgba(150, 190, 255, 90)}}}, 3.0f);
	drop.blendId = "normal";
	drop.bloom.amount = 0.0f;
	drop.trail.enabled = true;
	drop.trail.styleId = "ribbon";
	drop.trail.length = 0.06f;
	drop.trail.width = 0.8f;
	drop.trail.segments = 4;
	drop.fade.pathId = "constant";
	config.design.layers.push_back(drop);

	config.design.name = preset.name;
	return preset;
}

AtomPreset beatSparks()
{
	AtomPreset preset;
	preset.id = "beat_sparks";
	preset.name = "Beat Sparks";
	preset.category = "audio";
	preset.description = "Atom.Preset.BeatSparks.Description";

	EmitterConfig &config = preset.config;
	config.emission.shapeId = "line";
	config.emission.shapeParams.set("from", Vec2{0.1f, 0.85f});
	config.emission.shapeParams.set("to", Vec2{0.9f, 0.85f});
	config.emission.rate = 10.0f;

	config.physics.gravity = 420.0f;
	config.physics.initialSpeed = 120.0f;
	config.physics.emitAngle = -90.0f;
	config.physics.emitSpread = 45.0f;
	config.physics.drag = 1.0f;
	config.physics.lifetime = 1.4f;
	config.physics.alignToVelocity = true;
	config.physics.offset.speed = 0.5f;

	AtomLayer spark = makeLayer(
		"Spark", "soft_circle",
		Gradient{{{0.0f, rgba(255, 255, 240)}, {0.4f, rgba(120, 220, 255)}, {1.0f, rgba(40, 90, 255, 0)}}},
		5.0f);
	spark.bloom.amount = 0.9f;
	spark.bloom.radius = 3.0f;
	spark.trail.enabled = true;
	spark.trail.styleId = "streak";
	spark.trail.length = 0.12f;
	config.design.layers.push_back(spark);

	// Pick an audio source in the designer and these come alive: bass drives how many atoms are
	// launched, overall level drives how big they are.
	ModulationRoute rate;
	rate.modulatorId = "audio";
	rate.modulatorParams.set("band", std::string("low"));
	rate.modulatorParams.set("gain", 3.0);
	rate.target = "emission.rate";
	rate.modeId = "add";
	rate.amount = 420.0f;
	rate.smoothing = 0.04f;
	config.modulation.push_back(rate);

	ModulationRoute size;
	size.modulatorId = "audio";
	size.modulatorParams.set("band", std::string("level"));
	size.modulatorParams.set("gain", 2.5);
	size.target = "render.size_scale";
	size.modeId = "add";
	size.amount = 1.2f;
	size.smoothing = 0.08f;
	config.modulation.push_back(size);

	config.render.bloomModeId = "both";
	config.design.name = preset.name;
	return preset;
}

} // namespace

const std::vector<std::pair<std::string, std::string>> &presetCategories()
{
	static const std::vector<std::pair<std::string, std::string>> categories = {
		{"all", "Atom.Category.All"},
		{"fire", "Atom.Category.Fire"},
		{"smoke", "Atom.Category.Smoke"},
		{"magic", "Atom.Category.Magic"},
		{"celebration", "Atom.Category.Celebration"},
		{"weather", "Atom.Category.Weather"},
		{"audio", "Atom.Category.Audio"},
		{"user", "Atom.Category.User"},
	};
	return categories;
}

const std::vector<AtomPreset> &builtinPresets()
{
	static const std::vector<AtomPreset> presets = [] {
		std::vector<AtomPreset> list = {embers(), fire(),      sparks(),     flare(),    smoke(),
						fog(),    magicDust(), runeCircle(), confetti(), bubbles(),
						snow(),   rain(),      beatSparks()};
		for (AtomPreset &preset : list) {
			preset.builtin = true;
			preset.config.design.presetId = preset.id;
		}
		return list;
	}();
	return presets;
}

} // namespace atom
