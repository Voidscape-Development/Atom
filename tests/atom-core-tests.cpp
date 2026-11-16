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

// Exercises atom-core without OBS or Qt: every registry entry's defaults, the field tables and
// their nested visibility rules, the modulation engine, collision against scene objects, sprite
// sheet frames, and a 10-second run of every built-in preset.
//
// Configure with -DATOM_BUILD_TESTS=ON and run the `atom-core-tests` target. Most useful built
// with -fsanitize=address,undefined.

#include "atom-core/atom-presets.hpp"
#include "atom-core/atom-system.hpp"
#include "atom-core/atom-fields.hpp"
#include "atom-core/atom-modulation.hpp"

// These tests are assert-based, so keep assert() alive in release configurations too. Without
// this an optimized build would compile every check away and "pass" without testing anything.
#undef NDEBUG
#include <cassert>
#include <cstdio>

using namespace atom;

static void runPreset(const AtomPreset &preset)
{
	AtomSystem system;
	EmitterConfig config = preset.config;
	config.emission.width = 640;
	config.emission.height = 360;
	system.configure(config);

	SimContext context;
	context.hasTarget = true;
	context.target = Vec2{320.0f, 180.0f};

	float maxSize = 0.0f;
	size_t peak = 0;
	for (int i = 0; i < 600; ++i) {
		system.update(1.0f / 60.0f, context);
		peak = std::max(peak, system.count());
		for (const Atom &atom : system.atoms()) {
			const AtomVisual visual = system.evaluator().evaluate(atom);
			assert(visual.color.a >= 0.0f && visual.color.a <= 1.0f);
			assert(!std::isnan(visual.position.x) && !std::isnan(visual.position.y));
			assert(visual.size >= 0.0f);
			maxSize = std::max(maxSize, visual.size);
		}
	}

	printf("%-14s peak=%5zu live=%5zu maxSize=%6.1f layers=%zu trails=%d\n", preset.id.c_str(), peak,
	       system.count(), maxSize, preset.config.design.layers.size(), system.trailsEnabled() ? 1 : 0);
	assert(peak > 0);
	assert(static_cast<int>(peak) <= config.emission.maxAtoms);
}

static void testFieldRoundTrip()
{
	PhysicsConfig physics;
	physics.gravity = -321.0f;
	physics.endpoint.modeId = "point";
	physics.endpoint.point = Vec2{0.25f, 0.75f};
	physics.offset.hue = 0.5f;

	const ParamBag bag = toBag(physics, physicsFields());
	assert(bag.getFloat("gravity") == -321.0f);
	assert(bag.getString("endpoint_mode") == "point");
	assert(bag.getVec2("endpoint_point").y == 0.75f);
	assert(bag.getFloat("offset_hue") == 0.5f);

	PhysicsConfig restored;
	fromBag(restored, bag, physicsFields());
	assert(restored.gravity == physics.gravity);
	assert(restored.endpoint.modeId == "point");
	assert(restored.endpoint.point.x == 0.25f);
	assert(restored.offset.hue == 0.5f);

	// Visibility rules are prefixed when a nested table is spliced in.
	const ParamSchema schema = schemaOf(physicsFields());
	const ParamSpec *spec = findSpec(schema, "endpoint_point");
	assert(spec && spec->visibleWhen == "endpoint_mode=point");
	assert(evaluateVisibility(spec->visibleWhen, bag));

	const ParamSpec *arrival = findSpec(schema, "endpoint_arrival");
	assert(arrival && !evaluateVisibility("endpoint_mode=none", bag));
	assert(evaluateVisibility(arrival->visibleWhen, bag));

	printf("field round trip ok (%zu physics params, %zu layer params, %zu emission params)\n", schema.size(),
	       schemaOf(layerFields()).size(), schemaOf(emissionFields()).size());
}

static void testRegistries()
{
	registerBuiltinModules();
	for (const char *name :
	     {registries::kEmitterShape, registries::kBehavior, registries::kSprite, registries::kTrail,
	      registries::kFadePath, registries::kFalloff, registries::kEndpoint}) {
		const auto items = registryEnumItems(name);
		assert(!items.empty());
		for (const auto &item : items) {
			const ParamSchema schema = registrySchema(name, item.first);
			ParamBag bag;
			bag.applyDefaults(schema);
			assert(bag.values().size() == schema.size());
		}
		printf("registry %-14s %zu modules\n", name, items.size());
	}
}

static void testSprites()
{
	registerBuiltinModules();
	for (const auto &item : registryEnumItems(registries::kSprite)) {
		auto sprite = SpriteRegistry::instance().create(item.first);
		assert(sprite);
		ParamBag bag;
		bag.applyDefaults(registrySchema(registries::kSprite, item.first));
		sprite->configure(bag);

		float total = 0.0f;
		for (int y = 0; y < 32; ++y) {
			for (int x = 0; x < 32; ++x) {
				const float u = (x + 0.5f) / 16.0f - 1.0f;
				const float v = (y + 0.5f) / 16.0f - 1.0f;
				const float s = sprite->sample(u, v);
				assert(!std::isnan(s));
				total += saturate(s);
			}
		}
		assert(total > 0.0f);
	}
	printf("sprite masks ok\n");
}

static void testModulation()
{
	registerBuiltinModules();

	EmitterConfig base;
	base.emission.rate = 100.0f;
	base.physics.gravity = 100.0f;

	ModulationRoute rate;
	rate.modulatorId = "audio";
	rate.target = "emission.rate";
	rate.modeId = "add";
	rate.amount = 400.0f;
	rate.smoothing = 0.0f;

	ModulationRoute gravity;
	gravity.modulatorId = "constant";
	gravity.target = "physics.gravity";
	gravity.modeId = "multiply";
	gravity.amount = 3.0f;
	gravity.smoothing = 0.0f;

	ModulationEngine engine;
	engine.setRoutes({rate, gravity});

	ModContext context;
	context.dt = 1.0f / 60.0f;
	context.audio.valid = true;
	context.audio.level = 0.5f;

	EmitterConfig out;
	assert(engine.apply(base, context, out));
	assert(std::abs(out.emission.rate - 300.0f) < 0.01f);
	assert(std::abs(out.physics.gravity - 300.0f) < 0.01f);
	// The base is never modified, so routes do not compound frame to frame.
	assert(base.emission.rate == 100.0f);

	context.audio.level = 0.0f;
	assert(engine.apply(base, context, out));
	assert(std::abs(out.emission.rate - 100.0f) < 0.01f);

	assert(!modulationTargets().empty());
	printf("modulation ok (%zu targets)\n", modulationTargets().size());
}

/// The per-frame path writes into a live configuration instead of producing a copy. It has to give
/// the same answer as the copying path, and must not drift from frame to frame.
static void testModulationInPlace()
{
	EmitterConfig base;
	base.emission.rate = 100.0f;
	base.emission.maxAtoms = 500;
	base.physics.gravity = 100.0f;

	ModulationRoute rate;
	rate.modulatorId = "audio";
	rate.target = "emission.rate";
	rate.modeId = "add";
	rate.amount = 400.0f;
	rate.smoothing = 0.0f;

	// A second route on the same parameter stacks on the first, not on last frame's result.
	ModulationRoute boost = rate;
	boost.modulatorId = "constant";
	boost.modeId = "multiply";
	boost.amount = 2.0f;

	// An integer parameter stays an integer.
	ModulationRoute cap;
	cap.modulatorId = "constant";
	cap.target = "emission.max_atoms";
	cap.modeId = "replace";
	cap.amount = 50.0f;
	cap.smoothing = 0.0f;

	// Unknown targets are ignored rather than fatal.
	ModulationRoute bogus = cap;
	bogus.target = "physics.no_such_parameter";

	base.modulation = {rate, boost, cap, bogus};

	ModulationEngine engine;
	engine.setRoutes(base.modulation);
	ModulationEngine reference;
	reference.setRoutes(base.modulation);

	ModContext context;
	context.dt = 1.0f / 60.0f;
	context.audio.valid = true;

	EmitterConfig live = base;
	EmitterConfig copied;
	for (int frame = 0; frame < 120; ++frame) {
		context.audio.level = (frame % 3) * 0.25f;
		assert(engine.applyInPlace(base, context, live));
		assert(reference.apply(base, context, copied));

		const float expected = (100.0f + 400.0f * context.audio.level) * 2.0f;
		assert(std::abs(live.emission.rate - expected) < 0.01f);
		assert(live.emission.rate == copied.emission.rate);
		assert(live.emission.maxAtoms == 50 && copied.emission.maxAtoms == 50);
		assert(live.physics.gravity == base.physics.gravity);
	}
	assert(base.emission.rate == 100.0f && base.emission.maxAtoms == 500);

	// Through the system: values are picked up without a rebuild, and the atom limit is enforced
	// the moment modulation lowers it.
	base.emission.rate = 2000.0f;
	base.emission.prewarm = false;
	AtomSystem system;
	system.configure(base);
	for (int frame = 0; frame < 30; ++frame)
		system.update(1.0f / 60.0f, SimContext{});
	assert(system.count() > 50);

	context.audio.level = 0.0f;
	system.updateValues([&](EmitterConfig &config) { return engine.applyInPlace(base, context, config); });
	assert(system.config().emission.maxAtoms == 50);
	assert(system.count() <= 50);

	// No routes means nothing to do, and the live configuration is left alone.
	engine.setRoutes({});
	EmitterConfig untouched = base;
	untouched.emission.rate = 7.0f;
	assert(!engine.applyInPlace(base, context, untouched));
	assert(untouched.emission.rate == 7.0f);

	printf("in-place modulation ok\n");
}

static void testRotatedObjects()
{
	// A wide box turned a quarter turn is a tall box.
	SceneObject box;
	box.center = Vec2{100.0f, 100.0f};
	box.halfSize = Vec2{50.0f, 10.0f};
	box.rotation = 90.0f;

	Vec2 normal;
	assert(box.contains(Vec2{100.0f, 140.0f}));
	assert(!box.contains(Vec2{140.0f, 100.0f}));

	// Outside to the right: 30 px from the turned box's long side, pushed further right.
	const float distance = box.distance(Vec2{140.0f, 100.0f}, normal);
	assert(std::abs(distance - 30.0f) < 1e-3f);
	assert(normal.x > 0.99f && std::abs(normal.y) < 1e-3f);

	// The older, slower transforms must agree with the combined one distance() uses.
	const Vec2 local = box.toLocal(Vec2{140.0f, 100.0f});
	assert(std::abs(local.x) < 1e-3f && std::abs(local.y + 40.0f) < 1e-3f);

	printf("rotated objects ok\n");
}

static void testCollision()
{
	registerBuiltinModules();

	auto behavior = BehaviorRegistry::instance().create("source_collision");
	assert(behavior);
	ParamBag params;
	params.applyDefaults(registrySchema(registries::kBehavior, "source_collision"));
	behavior->configure(params);

	SceneObject box;
	box.center = Vec2{320.0f, 180.0f};
	box.halfSize = Vec2{60.0f, 40.0f};

	SimContext context;
	context.objects.push_back(box);

	Atom atom;
	atom.pos = Vec2{320.0f, 175.0f};
	atom.vel = Vec2{0.0f, 200.0f};
	behavior->apply(atom, 1.0f / 60.0f, context);

	// Pushed clear of the rectangle and moving away from it.
	Vec2 normal;
	assert(box.distance(atom.pos, normal) >= 0.0f);
	assert(atom.vel.y < 0.0f);

	// A point well outside is left alone.
	Atom far_;
	far_.pos = Vec2{10.0f, 10.0f};
	far_.vel = Vec2{5.0f, 5.0f};
	behavior->apply(far_, 1.0f / 60.0f, context);
	assert(far_.vel.x == 5.0f && far_.vel.y == 5.0f);

	printf("collision ok\n");
}

static void testSheets()
{
	AtomDesign design = AtomDesign::defaultDesign();
	design.layers[0].sheet.enabled = true;
	design.layers[0].sheet.columns = 4;
	design.layers[0].sheet.rows = 2;
	design.layers[0].sheet.modeId = "over_life";

	DesignEvaluator evaluator;
	evaluator.rebuild(design, PhysicsConfig{}, RenderConfig{});

	Atom atom;
	atom.life = 1.0f;
	atom.age = 0.0f;
	AtomVisual first = evaluator.evaluate(atom);
	assert(std::abs(first.u1 - first.u0 - 0.25f) < 1e-5f);
	assert(first.u0 == 0.0f && first.v0 == 0.0f);

	atom.age = 0.99f;
	AtomVisual last = evaluator.evaluate(atom);
	assert(last.u0 > 0.7f && last.v0 > 0.4f);

	printf("sprite sheets ok\n");
}

int main()
{
	registerBuiltinModules();
	testRegistries();
	testFieldRoundTrip();
	testSprites();
	testModulation();
	testModulationInPlace();
	testRotatedObjects();
	testCollision();
	testSheets();

	for (const AtomPreset &preset : builtinPresets())
		runPreset(preset);

	printf("all good\n");
	return 0;
}
