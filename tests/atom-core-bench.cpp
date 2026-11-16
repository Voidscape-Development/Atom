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

// Micro-benchmark for the parts of atom-core that run every frame: the simulation step, turning
// atoms into drawable values, and applying modulation. Build with -DATOM_BUILD_TESTS=ON in a
// release configuration and run `atom-core-bench`; numbers are microseconds per frame.

#include "atom-core/atom-modulation.hpp"
#include "atom-core/atom-presets.hpp"
#include "atom-core/atom-system.hpp"

#include <chrono>
#include <cstdio>

using namespace atom;

namespace {

using Clock = std::chrono::steady_clock;

double microsSince(Clock::time_point start)
{
	return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

constexpr int kFrames = 600;
constexpr float kStep = 1.0f / 60.0f;

/// A deliberately heavy emitter: a full pool, every built-in force and two layers with trails.
EmitterConfig stressConfig()
{
	EmitterConfig config;
	config.emission.rate = 2500.0f;
	config.emission.maxAtoms = 5000;
	config.emission.prewarm = true;
	config.emission.randomSeed = false;
	config.physics.lifetime = 2.0f;
	config.physics.turbulence = 120.0f;
	config.physics.drag = 0.6f;
	config.physics.endpoint.modeId = "point";
	config.physics.endpoint.arrivalId = "orbit";
	config.physics.endpoint.killOnArrival = false;

	for (const char *id : {"wind", "vortex", "bounds"}) {
		BehaviorInstance instance;
		instance.id = id;
		instance.params.applyDefaults(registrySchema(registries::kBehavior, id));
		config.physics.extras.push_back(instance);
	}

	config.design = AtomDesign::defaultDesign();
	AtomLayer second = config.design.layers.front();
	second.id = "second";
	second.fade.pathId = "flare";
	second.fade.flicker = 0.4f;
	config.design.layers.push_back(second);
	for (AtomLayer &layer : config.design.layers) {
		layer.trail.enabled = true;
		layer.trail.segments = 8;
		layer.offset.hue = 0.2f;
	}
	return config;
}

std::vector<ModulationRoute> stressRoutes()
{
	std::vector<ModulationRoute> routes;
	const char *targets[] = {"emission.rate", "physics.gravity", "physics.turbulence", "render.size_scale"};
	for (const char *target : targets) {
		ModulationRoute route;
		route.modulatorId = "lfo";
		route.modulatorParams.applyDefaults(registrySchema(registries::kModulator, "lfo"));
		route.target = target;
		route.modeId = "multiply";
		route.amount = 1.2f;
		routes.push_back(route);
	}
	return routes;
}

struct Result {
	double update = 0.0;
	double evaluate = 0.0;
	double modulate = 0.0;
	size_t atoms = 0;
	double checksum = 0.0;
};

Result run(EmitterConfig config, bool modulated)
{
	if (modulated)
		config.modulation = stressRoutes();

	AtomSystem system;
	system.configure(config);

	ModulationEngine engine;
	engine.setRoutes(config.modulation);

	Result result;
	float time = 0.0f;
	for (int frame = 0; frame < kFrames; ++frame) {
		time += kStep;

		ModContext modContext;
		modContext.time = time;
		modContext.dt = kStep;

		Clock::time_point start = Clock::now();
		system.updateValues([&](EmitterConfig &live) { return engine.applyInPlace(config, modContext, live); });
		result.modulate += microsSince(start);

		start = Clock::now();
		system.update(kStep, SimContext{});
		result.update += microsSince(start);

		// What a backend does each frame: one drawable per live atom.
		start = Clock::now();
		const DesignEvaluator &evaluator = system.evaluator();
		for (const Atom &atom : system.atoms()) {
			const AtomVisual visual = evaluator.evaluate(atom);
			result.checksum += visual.size + visual.color.a + visual.bloomAlpha;
		}
		result.evaluate += microsSince(start);
		result.atoms += system.count();
	}

	result.update /= kFrames;
	result.evaluate /= kFrames;
	result.modulate /= kFrames;
	result.atoms /= kFrames;
	return result;
}

void report(const char *name, const Result &result)
{
	printf("%-22s %6zu atoms  update %8.1f us  evaluate %8.1f us  modulate %7.2f us  (checksum %.3f)\n", name,
	       result.atoms, result.update, result.evaluate, result.modulate, result.checksum);
}

} // namespace

int main()
{
	registerBuiltinModules();

	report("stress", run(stressConfig(), false));
	report("stress + modulation", run(stressConfig(), true));

	for (const AtomPreset &preset : builtinPresets()) {
		EmitterConfig config = preset.config;
		config.emission.randomSeed = false;
		report(preset.id.c_str(), run(config, false));
	}
	return 0;
}
