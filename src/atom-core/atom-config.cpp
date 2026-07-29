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

#include "atom-config.hpp"
#include "atom-random.hpp"

#include <atomic>

namespace atom {

namespace {

using Items = std::vector<std::pair<std::string, std::string>>;

Items emissionModeItems()
{
	return {{"continuous", "Atom.Emission.Mode.Continuous"},
		{"burst", "Atom.Emission.Mode.Burst"},
		{"both", "Atom.Emission.Mode.Both"}};
}

Items directionModeItems()
{
	return {{"angle", "Atom.Emission.Direction.Angle"},     {"shape_normal", "Atom.Emission.Direction.ShapeNormal"},
		{"outward", "Atom.Emission.Direction.Outward"}, {"inward", "Atom.Emission.Direction.Inward"},
		{"random", "Atom.Emission.Direction.Random"},   {"endpoint", "Atom.Emission.Direction.Endpoint"}};
}

Items arrivalItems()
{
	return {{"attract", "Atom.Endpoint.Arrival.Attract"},
		{"arrive", "Atom.Endpoint.Arrival.Arrive"},
		{"orbit", "Atom.Endpoint.Arrival.Orbit"}};
}

Items anchorItems()
{
	return {{"center", "Atom.Endpoint.Anchor.Center"},
		{"top_left", "Atom.Endpoint.Anchor.TopLeft"},
		{"top_right", "Atom.Endpoint.Anchor.TopRight"},
		{"bottom_left", "Atom.Endpoint.Anchor.BottomLeft"},
		{"bottom_right", "Atom.Endpoint.Anchor.BottomRight"},
		{"random", "Atom.Endpoint.Anchor.Random"}};
}

Items colorModeItems()
{
	return {{"solid", "Atom.Design.Color.Mode.Solid"},
		{"gradient", "Atom.Design.Color.Mode.Gradient"},
		{"random_stop", "Atom.Design.Color.Mode.RandomStop"}};
}

Items sheetModeItems()
{
	return {{"over_life", "Atom.Design.Sheet.Mode.OverLife"},
		{"loop", "Atom.Design.Sheet.Mode.Loop"},
		{"random", "Atom.Design.Sheet.Mode.Random"}};
}

Items bloomModeItems()
{
	return {{"per_atom", "Atom.Render.Bloom.Mode.PerAtom"},
		{"post", "Atom.Render.Bloom.Mode.Post"},
		{"both", "Atom.Render.Bloom.Mode.Both"}};
}

Items routeModeItems()
{
	return {{"add", "Atom.Modulation.Mode.Add"},
		{"multiply", "Atom.Modulation.Mode.Multiply"},
		{"replace", "Atom.Modulation.Mode.Replace"}};
}

Items blendItems()
{
	return {{"normal", "Atom.Design.Blend.Normal"}, {"additive", "Atom.Design.Blend.Additive"}};
}

const FieldTable<EndpointConfig> &endpointFields()
{
	static const FieldTable<EndpointConfig> fields = [] {
		FieldTable<EndpointConfig> f;
		f.push_back(bindRegistryEnum<EndpointConfig>("mode", "Atom.Endpoint.Mode", &EndpointConfig::modeId,
							     registries::kEndpoint));
		f.push_back(visibleWhen(bindVec2<EndpointConfig>("point", "Atom.Endpoint.Point", &EndpointConfig::point,
								 0.0, 1.0, 0.001),
					"mode=point"));
		f.push_back(visibleWhen(bindText<EndpointConfig>("source", "Atom.Endpoint.Source",
								 &EndpointConfig::sourceName, ParamType::SourceRef),
					"mode=source"));
		f.push_back(visibleWhen(bindEnum<EndpointConfig>("anchor", "Atom.Endpoint.Anchor",
								 &EndpointConfig::anchorId, anchorItems()),
					"mode=source"));
		f.push_back(visibleWhen(bindEnum<EndpointConfig>("arrival", "Atom.Endpoint.Arrival",
								 &EndpointConfig::arrivalId, arrivalItems()),
					"mode!=none"));
		f.push_back(visibleWhen(bindFloat<EndpointConfig>("strength", "Atom.Endpoint.Strength",
								  &EndpointConfig::strength, 0.0, 8.0, 0.01),
					"mode!=none"));
		f.push_back(
			visibleWhen(bindFloat<EndpointConfig>("radius", "Atom.Endpoint.Radius",
							      &EndpointConfig::arriveRadius, 0.0, 400.0, 1.0, {}, "px"),
				    "mode!=none"));
		f.push_back(visibleWhen(bindBool<EndpointConfig>("kill", "Atom.Endpoint.KillOnArrival",
								 &EndpointConfig::killOnArrival),
					"mode!=none"));
		f.push_back(visibleWhen(bindFloat<EndpointConfig>("scatter", "Atom.Endpoint.Scatter",
								  &EndpointConfig::scatter, 0.0, 500.0, 1.0, {}, "px"),
					"mode!=none"));
		return f;
	}();
	return fields;
}

const FieldTable<OffsetConfig> &offsetFields()
{
	static const FieldTable<OffsetConfig> fields = [] {
		FieldTable<OffsetConfig> f;
		f.push_back(bindFloat<OffsetConfig>("position", "Atom.Offset.Position", &OffsetConfig::position, 0.0,
						    500.0, 1.0, {}, "px"));
		f.push_back(bindFloat<OffsetConfig>("angle", "Atom.Offset.Angle", &OffsetConfig::angle, 0.0, 180.0, 1.0,
						    {}, "\xC2\xB0"));
		f.push_back(
			bindFloat<OffsetConfig>("speed", "Atom.Offset.Speed", &OffsetConfig::speed, 0.0, 1.0, 0.01));
		f.push_back(bindFloat<OffsetConfig>("size", "Atom.Offset.Size", &OffsetConfig::size, 0.0, 1.0, 0.01));
		f.push_back(bindFloat<OffsetConfig>("lifetime", "Atom.Offset.Lifetime", &OffsetConfig::lifetime, 0.0,
						    1.0, 0.01));
		f.push_back(bindFloat<OffsetConfig>("rotation", "Atom.Offset.Rotation", &OffsetConfig::rotation, 0.0,
						    1.0, 0.01));
		f.push_back(bindFloat<OffsetConfig>("hue", "Atom.Offset.Hue", &OffsetConfig::hue, 0.0, 1.0, 0.01));
		f.push_back(bindFloat<OffsetConfig>("brightness", "Atom.Offset.Brightness", &OffsetConfig::brightness,
						    0.0, 1.0, 0.01));
		f.push_back(
			bindFloat<OffsetConfig>("alpha", "Atom.Offset.Alpha", &OffsetConfig::alpha, 0.0, 1.0, 0.01));
		f.push_back(bindFloat<OffsetConfig>("spawn_phase", "Atom.Offset.SpawnPhase", &OffsetConfig::spawnPhase,
						    0.0, 1.0, 0.01));
		return f;
	}();
	return fields;
}

const FieldTable<ColorConfig> &colorFields()
{
	static const FieldTable<ColorConfig> fields = [] {
		FieldTable<ColorConfig> f;
		f.push_back(bindEnum<ColorConfig>("mode", "Atom.Design.Color.Mode", &ColorConfig::modeId,
						  colorModeItems()));
		f.push_back(visibleWhen(bindColor<ColorConfig>("color", "Atom.Design.Color.Solid", &ColorConfig::color),
					"mode=solid"));
		f.push_back(visibleWhen(bindGradient<ColorConfig>("gradient", "Atom.Design.Color.Gradient",
								  &ColorConfig::gradient),
					"mode!=solid"));
		return f;
	}();
	return fields;
}

const FieldTable<BloomConfig> &bloomFields()
{
	static const FieldTable<BloomConfig> fields = [] {
		FieldTable<BloomConfig> f;
		f.push_back(bindFloat<BloomConfig>("amount", "Atom.Design.Bloom.Amount", &BloomConfig::amount, 0.0, 1.0,
						   0.01));
		f.push_back(bindFloat<BloomConfig>("radius", "Atom.Design.Bloom.Radius", &BloomConfig::radius, 1.0, 8.0,
						   0.05));
		f.push_back(bindFloat<BloomConfig>("softness", "Atom.Design.Bloom.Softness", &BloomConfig::softness,
						   0.0, 1.0, 0.01));
		f.push_back(
			bindBool<BloomConfig>("inherit", "Atom.Design.Bloom.InheritColor", &BloomConfig::inheritColor));
		f.push_back(visibleWhen(bindColor<BloomConfig>("tint", "Atom.Design.Bloom.Tint", &BloomConfig::tint),
					"inherit=false"));
		return f;
	}();
	return fields;
}

const FieldTable<SizeConfig> &sizeFields()
{
	static const FieldTable<SizeConfig> fields = [] {
		FieldTable<SizeConfig> f;
		f.push_back(bindFloat<SizeConfig>("base", "Atom.Design.Size.Base", &SizeConfig::base, 0.5, 512.0, 0.5,
						  {}, "px", true));
		f.push_back(bindFloat<SizeConfig>("minimum", "Atom.Design.Size.Minimum", &SizeConfig::minimum, 0.0,
						  64.0, 0.5, {}, "px"));
		f.push_back(bindCurve<SizeConfig>("over_life", "Atom.Design.Size.OverLife", &SizeConfig::overLife));
		return f;
	}();
	return fields;
}

const FieldTable<TrailConfig> &trailFields()
{
	static const FieldTable<TrailConfig> fields = [] {
		FieldTable<TrailConfig> f;
		f.push_back(bindBool<TrailConfig>("enabled", "Atom.Design.Trail.Enabled", &TrailConfig::enabled));
		f.push_back(visibleWhen(bindRegistryEnum<TrailConfig>("style", "Atom.Design.Trail.Style",
								      &TrailConfig::styleId, registries::kTrail),
					"enabled=true"));
		f.push_back(visibleWhen(bindFloat<TrailConfig>("length", "Atom.Design.Trail.Length",
							       &TrailConfig::length, 0.02, 2.0, 0.01, {}, "s"),
					"enabled=true"));
		f.push_back(visibleWhen(bindFloat<TrailConfig>("width", "Atom.Design.Trail.Width", &TrailConfig::width,
							       0.05, 4.0, 0.01),
					"enabled=true"));
		f.push_back(visibleWhen(bindFloat<TrailConfig>("fade", "Atom.Design.Trail.Fade", &TrailConfig::fade,
							       0.0, 1.0, 0.01),
					"enabled=true"));
		f.push_back(visibleWhen(bindInt<TrailConfig>("segments", "Atom.Design.Trail.Segments",
							     &TrailConfig::segments, 2, 32, 1),
					"enabled=true"));
		f.push_back(visibleWhen(bindBool<TrailConfig>("inherit", "Atom.Design.Trail.InheritColor",
							      &TrailConfig::inheritColor),
					"enabled=true"));
		f.push_back(visibleWhen(bindColor<TrailConfig>("tint", "Atom.Design.Trail.Tint", &TrailConfig::tint),
					"inherit=false"));
		return f;
	}();
	return fields;
}

const FieldTable<FadeConfig> &fadeFields()
{
	static const FieldTable<FadeConfig> fields = [] {
		FieldTable<FadeConfig> f;
		f.push_back(bindRegistryEnum<FadeConfig>("path", "Atom.Design.Fade.Path", &FadeConfig::pathId,
							 registries::kFadePath));
		f.push_back(
			visibleWhen(bindCurve<FadeConfig>("curve", "Atom.Design.Fade.Curve", &FadeConfig::customCurve),
				    "path=custom"));
		f.push_back(bindFloat<FadeConfig>("fade_in", "Atom.Design.Fade.FadeIn", &FadeConfig::fadeIn, 0.0, 0.9,
						  0.01));
		f.push_back(bindFloat<FadeConfig>("flicker", "Atom.Design.Fade.Flicker", &FadeConfig::flicker, 0.0, 1.0,
						  0.01));
		f.push_back(bindFloat<FadeConfig>("flicker_speed", "Atom.Design.Fade.FlickerSpeed",
						  &FadeConfig::flickerSpeed, 0.1, 60.0, 0.1, {}, "Hz"));
		return f;
	}();
	return fields;
}

const FieldTable<SheetConfig> &sheetFields()
{
	static const FieldTable<SheetConfig> fields = [] {
		FieldTable<SheetConfig> f;
		f.push_back(bindBool<SheetConfig>("enabled", "Atom.Design.Sheet.Enabled", &SheetConfig::enabled));
		f.push_back(visibleWhen(bindInt<SheetConfig>("columns", "Atom.Design.Sheet.Columns",
							     &SheetConfig::columns, 1, 64, 1),
					"enabled=true"));
		f.push_back(visibleWhen(bindInt<SheetConfig>("rows", "Atom.Design.Sheet.Rows", &SheetConfig::rows, 1,
							     64, 1),
					"enabled=true"));
		f.push_back(visibleWhen(bindEnum<SheetConfig>("mode", "Atom.Design.Sheet.Mode", &SheetConfig::modeId,
							      sheetModeItems()),
					"enabled=true"));
		f.push_back(visibleWhen(bindFloat<SheetConfig>("fps", "Atom.Design.Sheet.Fps", &SheetConfig::fps, 0.1,
							       120.0, 0.1, {}, "fps"),
					"mode=loop"));
		f.push_back(visibleWhen(bindInt<SheetConfig>("first", "Atom.Design.Sheet.FirstFrame",
							     &SheetConfig::firstFrame, 0, 4095, 1),
					"enabled=true"));
		f.push_back(visibleWhen(bindInt<SheetConfig>("last", "Atom.Design.Sheet.LastFrame",
							     &SheetConfig::lastFrame, -1, 4095, 1),
					"enabled=true"));
		return f;
	}();
	return fields;
}

} // namespace

const FieldTable<EmissionConfig> &emissionFields()
{
	static const FieldTable<EmissionConfig> fields = [] {
		FieldTable<EmissionConfig> f;
		f.push_back(bindInt<EmissionConfig>("width", "Atom.Emission.Width", &EmissionConfig::width, 1.0, 8192.0,
						    1.0, "size", "px"));
		f.push_back(bindInt<EmissionConfig>("height", "Atom.Emission.Height", &EmissionConfig::height, 1.0,
						    8192.0, 1.0, "size", "px"));
		f.push_back(bindRegistryEnum<EmissionConfig>("shape", "Atom.Emission.Shape", &EmissionConfig::shapeId,
							     registries::kEmitterShape, "location"));
		f.push_back(bindEnum<EmissionConfig>("direction_mode", "Atom.Emission.DirectionMode",
						     &EmissionConfig::directionModeId, directionModeItems(),
						     "location"));
		f.push_back(bindEnum<EmissionConfig>("mode", "Atom.Emission.Mode", &EmissionConfig::modeId,
						     emissionModeItems(), "rate"));
		f.push_back(visibleWhen(bindFloat<EmissionConfig>("rate", "Atom.Emission.Rate", &EmissionConfig::rate,
								  0.0, 5000.0, 1.0, "rate", "/s"),
					"mode!=burst"));
		f.push_back(visibleWhen(bindInt<EmissionConfig>("burst_count", "Atom.Emission.BurstCount",
								&EmissionConfig::burstCount, 1.0, 5000.0, 1.0, "rate"),
					"mode!=continuous"));
		f.push_back(visibleWhen(bindFloat<EmissionConfig>("burst_interval", "Atom.Emission.BurstInterval",
								  &EmissionConfig::burstInterval, 0.05, 60.0, 0.05,
								  "rate", "s"),
					"mode!=continuous"));
		f.push_back(bindInt<EmissionConfig>("max_atoms", "Atom.Emission.MaxAtoms", &EmissionConfig::maxAtoms,
						    1.0, 100000.0, 1.0, "rate"));
		f.push_back(
			bindBool<EmissionConfig>("prewarm", "Atom.Emission.Prewarm", &EmissionConfig::prewarm, "rate"));
		f.push_back(bindBool<EmissionConfig>("random_seed", "Atom.Emission.RandomSeed",
						     &EmissionConfig::randomSeed, "rate"));
		f.push_back(visibleWhen(bindInt<EmissionConfig>("seed", "Atom.Emission.Seed", &EmissionConfig::seed,
								0.0, 1000000.0, 1.0, "rate"),
					"random_seed=false"));
		return f;
	}();
	return fields;
}

const FieldTable<PhysicsConfig> &physicsFields()
{
	static const FieldTable<PhysicsConfig> fields = [] {
		FieldTable<PhysicsConfig> f;
		f.push_back(
			described(bindFloat<PhysicsConfig>("gravity", "Atom.Physics.Gravity", &PhysicsConfig::gravity,
							   -2000.0, 2000.0, 1.0, "motion", "px/s\xC2\xB2"),
				  "Atom.Physics.Gravity.Description"));
		f.push_back(bindFloat<PhysicsConfig>("gravity_angle", "Atom.Physics.GravityAngle",
						     &PhysicsConfig::gravityAngle, -360.0, 360.0, 1.0, "motion",
						     "\xC2\xB0"));
		f.push_back(bindFloat<PhysicsConfig>("speed", "Atom.Physics.InitialSpeed", &PhysicsConfig::initialSpeed,
						     0.0, 4000.0, 1.0, "motion", "px/s", true));
		f.push_back(bindFloat<PhysicsConfig>("emit_angle", "Atom.Physics.EmitAngle", &PhysicsConfig::emitAngle,
						     -360.0, 360.0, 1.0, "motion", "\xC2\xB0"));
		f.push_back(bindFloat<PhysicsConfig>("emit_spread", "Atom.Physics.EmitSpread",
						     &PhysicsConfig::emitSpread, 0.0, 360.0, 1.0, "motion",
						     "\xC2\xB0"));
		f.push_back(bindFloat<PhysicsConfig>("drag", "Atom.Physics.Drag", &PhysicsConfig::drag, 0.0, 10.0, 0.01,
						     "motion"));
		f.push_back(bindFloat<PhysicsConfig>("turbulence", "Atom.Physics.Turbulence",
						     &PhysicsConfig::turbulence, 0.0, 2000.0, 1.0, "motion"));
		f.push_back(visibleWhen(bindFloat<PhysicsConfig>("turbulence_scale", "Atom.Physics.TurbulenceScale",
								 &PhysicsConfig::turbulenceScale, 8.0, 2000.0, 1.0,
								 "motion", "px"),
					"turbulence!=0"));
		f.push_back(visibleWhen(bindFloat<PhysicsConfig>("turbulence_speed", "Atom.Physics.TurbulenceSpeed",
								 &PhysicsConfig::turbulenceSpeed, 0.0, 10.0, 0.05,
								 "motion"),
					"turbulence!=0"));
		f.push_back(bindFloat<PhysicsConfig>("lifetime", "Atom.Physics.Lifetime", &PhysicsConfig::lifetime,
						     0.05, 120.0, 0.05, "life", "s", true));
		f.push_back(bindRegistryEnum<PhysicsConfig>("falloff", "Atom.Physics.LifetimeFalloff",
							    &PhysicsConfig::falloffId, registries::kFalloff, "life"));
		f.push_back(visibleWhen(bindCurve<PhysicsConfig>("falloff_curve", "Atom.Physics.FalloffCurve",
								 &PhysicsConfig::falloffCurve, "life"),
					"falloff=custom"));
		f.push_back(bindFloat<PhysicsConfig>("spin", "Atom.Physics.Spin", &PhysicsConfig::spin, -720.0, 720.0,
						     1.0, "motion", "\xC2\xB0/s", true));
		f.push_back(bindBool<PhysicsConfig>("align_to_velocity", "Atom.Physics.AlignToVelocity",
						    &PhysicsConfig::alignToVelocity, "motion"));

		appendNested<PhysicsConfig, EndpointConfig>(f, endpointFields(), &PhysicsConfig::endpoint, "endpoint_",
							    "endpoint");
		appendNested<PhysicsConfig, OffsetConfig>(f, offsetFields(), &PhysicsConfig::offset, "offset_",
							  "offset");
		return f;
	}();
	return fields;
}

const FieldTable<AtomLayer> &layerFields()
{
	static const FieldTable<AtomLayer> fields = [] {
		FieldTable<AtomLayer> f;
		f.push_back(bindText<AtomLayer>("name", "Atom.Design.Layer.Name", &AtomLayer::name, ParamType::Text,
						"layer"));
		f.push_back(bindBool<AtomLayer>("enabled", "Atom.Design.Layer.Enabled", &AtomLayer::enabled, "layer"));
		f.push_back(bindFloat<AtomLayer>("weight", "Atom.Design.Layer.Weight", &AtomLayer::weight, 0.0, 100.0,
						 0.1, "layer"));
		f.push_back(bindRegistryEnum<AtomLayer>("sprite", "Atom.Design.Sprite", &AtomLayer::spriteId,
							registries::kSprite, "layer"));
		f.push_back(visibleWhen(bindText<AtomLayer>("image", "Atom.Design.Image", &AtomLayer::imagePath,
							    ParamType::Path, "layer",
							    "Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"),
					"sprite=image"));
		f.push_back(
			bindEnum<AtomLayer>("blend", "Atom.Design.Blend", &AtomLayer::blendId, blendItems(), "layer"));

		appendNested<AtomLayer, ColorConfig>(f, colorFields(), &AtomLayer::color, "color_", "color");
		appendNested<AtomLayer, BloomConfig>(f, bloomFields(), &AtomLayer::bloom, "bloom_", "bloom");
		appendNested<AtomLayer, SizeConfig>(f, sizeFields(), &AtomLayer::size, "size_", "size");
		appendNested<AtomLayer, TrailConfig>(f, trailFields(), &AtomLayer::trail, "trail_", "trail");
		appendNested<AtomLayer, FadeConfig>(f, fadeFields(), &AtomLayer::fade, "fade_", "fade");
		appendNested<AtomLayer, SheetConfig>(f, sheetFields(), &AtomLayer::sheet, "sheet_", "sheet");
		return f;
	}();
	return fields;
}

const FieldTable<RenderConfig> &renderFields()
{
	static const FieldTable<RenderConfig> fields = [] {
		FieldTable<RenderConfig> f;
		f.push_back(described(bindEnum<RenderConfig>("bloom_mode", "Atom.Render.Bloom.Mode",
							     &RenderConfig::bloomModeId, bloomModeItems(), "bloom"),
				      "Atom.Render.Bloom.Mode.Description"));
		f.push_back(visibleWhen(bindFloat<RenderConfig>("bloom_threshold", "Atom.Render.Bloom.Threshold",
								&RenderConfig::bloomThreshold, 0.0, 1.0, 0.01, "bloom"),
					"bloom_mode!=per_atom"));
		f.push_back(visibleWhen(bindFloat<RenderConfig>("bloom_intensity", "Atom.Render.Bloom.Intensity",
								&RenderConfig::bloomIntensity, 0.0, 4.0, 0.01, "bloom"),
					"bloom_mode!=per_atom"));
		f.push_back(
			visibleWhen(bindFloat<RenderConfig>("bloom_radius", "Atom.Render.Bloom.Radius",
							    &RenderConfig::bloomRadius, 0.5, 64.0, 0.5, "bloom", "px"),
				    "bloom_mode!=per_atom"));
		f.push_back(visibleWhen(bindInt<RenderConfig>("bloom_iterations", "Atom.Render.Bloom.Iterations",
							      &RenderConfig::bloomIterations, 1, 6, 1, "bloom"),
					"bloom_mode!=per_atom"));
		f.push_back(visibleWhen(bindInt<RenderConfig>("bloom_downscale", "Atom.Render.Bloom.Downscale",
							      &RenderConfig::bloomDownscale, 1, 8, 1, "bloom"),
					"bloom_mode!=per_atom"));
		f.push_back(bindFloat<RenderConfig>("size_scale", "Atom.Render.SizeScale", &RenderConfig::sizeScale,
						    0.0, 8.0, 0.01, "global"));
		f.push_back(bindFloat<RenderConfig>("bloom_scale", "Atom.Render.BloomScale", &RenderConfig::bloomScale,
						    0.0, 8.0, 0.01, "global"));
		f.push_back(bindFloat<RenderConfig>("opacity", "Atom.Render.Opacity", &RenderConfig::opacity, 0.0, 1.0,
						    0.01, "global"));
		f.push_back(described(bindFloat<RenderConfig>("time_scale", "Atom.Render.TimeScale",
							      &RenderConfig::timeScale, 0.0, 4.0, 0.01, "global"),
				      "Atom.Render.TimeScale.Description"));
		return f;
	}();
	return fields;
}

const FieldTable<SceneConfig> &sceneFields()
{
	static const FieldTable<SceneConfig> fields = [] {
		FieldTable<SceneConfig> f;
		f.push_back(bindBool<SceneConfig>("track_all", "Atom.Scene.TrackAll", &SceneConfig::trackAll, "scene"));
		f.push_back(
			visibleWhen(described(bindText<SceneConfig>("sources", "Atom.Scene.Sources",
								    &SceneConfig::sources, ParamType::Text, "scene"),
					      "Atom.Scene.Sources.Description"),
				    "track_all=false"));
		f.push_back(bindFloat<SceneConfig>("padding", "Atom.Scene.Padding", &SceneConfig::padding, -200.0,
						   200.0, 1.0, "scene", "px"));
		return f;
	}();
	return fields;
}

const FieldTable<AudioConfig> &audioFields()
{
	static const FieldTable<AudioConfig> fields = [] {
		FieldTable<AudioConfig> f;
		f.push_back(described(bindText<AudioConfig>("audio_source", "Atom.Audio.Source",
							    &AudioConfig::sourceName, ParamType::SourceRef, "audio"),
				      "Atom.Audio.Source.Description"));
		f.push_back(bindFloat<AudioConfig>("audio_gain", "Atom.Audio.Gain", &AudioConfig::gain, 0.0, 16.0, 0.05,
						   "audio"));
		f.push_back(bindFloat<AudioConfig>("audio_attack", "Atom.Audio.Attack", &AudioConfig::attack, 0.0, 1.0,
						   0.005, "audio", "s"));
		f.push_back(bindFloat<AudioConfig>("audio_release", "Atom.Audio.Release", &AudioConfig::release, 0.0,
						   3.0, 0.005, "audio", "s"));
		return f;
	}();
	return fields;
}

const FieldTable<ModulationRoute> &routeFields()
{
	static const FieldTable<ModulationRoute> fields = [] {
		FieldTable<ModulationRoute> f;
		f.push_back(bindBool<ModulationRoute>("enabled", "Atom.Modulation.Enabled", &ModulationRoute::enabled));
		f.push_back(bindRegistryEnum<ModulationRoute>("modulator", "Atom.Modulation.Modulator",
							      &ModulationRoute::modulatorId, registries::kModulator));
		f.push_back(bindEnum<ModulationRoute>("target", "Atom.Modulation.Target", &ModulationRoute::target,
						      modulationTargets()));
		f.push_back(bindEnum<ModulationRoute>("mode", "Atom.Modulation.Mode", &ModulationRoute::modeId,
						      routeModeItems()));
		f.push_back(described(bindFloat<ModulationRoute>("amount", "Atom.Modulation.Amount",
								 &ModulationRoute::amount, -10000.0, 10000.0, 0.01),
				      "Atom.Modulation.Amount.Description"));
		f.push_back(bindFloat<ModulationRoute>("smoothing", "Atom.Modulation.Smoothing",
						       &ModulationRoute::smoothing, 0.0, 2.0, 0.01, {}, "s"));
		return f;
	}();
	return fields;
}

const std::vector<std::pair<std::string, std::string>> &modulationTargets()
{
	// Anything numeric in these tables can be driven, so binding a new option makes it
	// modulatable with no extra work.
	static const std::vector<std::pair<std::string, std::string>> targets = [] {
		std::vector<std::pair<std::string, std::string>> list;
		const auto append = [&list](const ParamSchema &schema, const std::string &prefix) {
			for (const ParamSpec &spec : schema) {
				if (spec.type != ParamType::Float && spec.type != ParamType::Int)
					continue;
				list.emplace_back(prefix + spec.id, spec.label);
			}
		};
		append(schemaOf(emissionFields()), "emission.");
		append(schemaOf(physicsFields()), "physics.");
		append(schemaOf(renderFields()), "render.");
		return list;
	}();
	return targets;
}

std::string makeLayerId()
{
	static std::atomic<uint32_t> counter{1};
	static Random random(0xA70Du);

	const uint32_t n = counter.fetch_add(1);
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "layer_%08x%04x", random.nextUint(), static_cast<unsigned>(n & 0xFFFFu));
	return buffer;
}

AtomDesign AtomDesign::defaultDesign()
{
	AtomDesign design;
	design.name = "Embers";

	AtomLayer ember;
	ember.id = makeLayerId();
	ember.name = "Ember";
	ember.spriteId = "soft_circle";
	ember.blendId = "additive";
	ember.color.modeId = "gradient";
	ember.size.base = 10.0f;
	ember.size.overLife = Curve{{{0.0f, 0.35f}, {0.15f, 1.0f}, {1.0f, 0.25f}}};
	ember.bloom.amount = 0.55f;
	ember.bloom.radius = 2.4f;
	ember.fade.pathId = "ease_out";
	ember.fade.flicker = 0.25f;
	design.layers.push_back(ember);

	return design;
}

AtomLayer *AtomDesign::findLayer(const std::string &id)
{
	for (AtomLayer &layer : layers) {
		if (layer.id == id)
			return &layer;
	}
	return nullptr;
}

const AtomLayer *AtomDesign::findLayer(const std::string &id) const
{
	return const_cast<AtomDesign *>(this)->findLayer(id);
}

size_t AtomDesign::pickLayer(float roll) const
{
	float total = 0.0f;
	for (const AtomLayer &layer : layers) {
		if (layer.enabled)
			total += std::max(0.0f, layer.weight);
	}

	if (total <= 0.0f) {
		for (size_t i = 0; i < layers.size(); ++i) {
			if (layers[i].enabled)
				return i;
		}
		return 0;
	}

	float cursor = saturate(roll) * total;
	for (size_t i = 0; i < layers.size(); ++i) {
		if (!layers[i].enabled)
			continue;
		cursor -= std::max(0.0f, layers[i].weight);
		if (cursor <= 0.0f)
			return i;
	}

	for (size_t i = layers.size(); i-- > 0;) {
		if (layers[i].enabled)
			return i;
	}
	return 0;
}

bool AtomDesign::anyTrails() const
{
	for (const AtomLayer &layer : layers) {
		if (layer.enabled && layer.trail.enabled)
			return true;
	}
	return false;
}

} // namespace atom
