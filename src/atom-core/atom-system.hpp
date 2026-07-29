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
#include "atom-modules.hpp"
#include "atom-visual.hpp"

#include <memory>
#include <vector>

namespace atom {

/// The simulation. Owns the atom pool, the configured modules and the emission schedule.
///
/// Contains no OBS calls at all, which is what lets the designer window run the exact same
/// simulation as the source it is editing.
class AtomSystem {
public:
	AtomSystem();
	~AtomSystem();

	AtomSystem(const AtomSystem &) = delete;
	AtomSystem &operator=(const AtomSystem &) = delete;

	/// Applies a new configuration, rebuilding modules. Live atoms are kept unless the emitter
	/// shape or the design's layer count changed in a way that invalidates them.
	void configure(const EmitterConfig &config);

	/// Applies a per-frame modulated configuration. Identical to configure() except that it
	/// never prewarms, so a modulated emitter does not restart itself sixty times a second.
	void setModulatedConfig(const EmitterConfig &config) { applyConfig(config, false); }

	const EmitterConfig &config() const { return config_; }

	/// Advances the simulation. `context` only needs the host-resolved fields filled in
	/// (`hasTarget` / `target`); size and time are taken from the config and the internal clock.
	void update(float dt, SimContext context);

	/// Emits `count` atoms immediately, regardless of the emission mode.
	void burst(int count);

	/// Removes every live atom and resets the emission schedule.
	void reset();

	/// Runs the simulation forward without drawing, so a freshly created emitter already looks
	/// settled. Called automatically by configure() when prewarm is enabled.
	void prewarm(const SimContext &context);

	void setEmitting(bool emitting) { emitting_ = emitting; }
	bool emitting() const { return emitting_; }

	const std::vector<Atom> &atoms() const { return atoms_; }
	const std::vector<TrailHistory> &trails() const { return trails_; }
	bool trailsEnabled() const { return trailsEnabled_; }
	size_t count() const { return atoms_.size(); }

	const DesignEvaluator &evaluator() const { return evaluator_; }

	/// True when the configured endpoint needs the host to look up an OBS source and fill
	/// SimContext::target.
	bool needsHostTarget() const;
	const std::string &targetSourceName() const { return config_.physics.endpoint.sourceName; }

	/// Emitter surface size in pixels, straight from the config.
	float width() const { return static_cast<float>(config_.emission.width); }
	float height() const { return static_cast<float>(config_.emission.height); }

private:
	void applyConfig(const EmitterConfig &config, bool allowPrewarm);
	void rebuildModules();
	void spawn(size_t count, const SimContext &context);
	void spawnOne(const SimContext &context);
	void applyForces(Atom &atom, float dt, const SimContext &context);
	void applyEndpoint(Atom &atom, float dt);
	void recordTrail(size_t index, float dt);
	void retire(size_t index);
	Vec2 endpointFor(const Atom &atom) const;

	EmitterConfig config_;
	DesignEvaluator evaluator_;

	std::unique_ptr<EmitterShape> shape_;
	std::unique_ptr<EndpointProvider> endpoint_;
	std::vector<std::unique_ptr<Behavior>> behaviors_;

	std::vector<Atom> atoms_;
	std::vector<TrailHistory> trails_;
	bool trailsEnabled_ = false;

	Random random_;
	SimContext context_;

	float time_ = 0.0f;
	float spawnAccumulator_ = 0.0f;
	float burstTimer_ = 0.0f;
	bool emitting_ = true;
	bool configured_ = false;
	bool hasEndpointTarget_ = false;
	Vec2 endpointTarget_;
};

} // namespace atom
