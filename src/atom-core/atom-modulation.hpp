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
#include "atom-registry.hpp"

#include <memory>

namespace atom {

/// Audio the host has measured this frame, split into three coarse bands.
///
/// The core does no DSP: the host fills this in and modulators just read it, the same way the
/// endpoint providers read a host-resolved target.
struct AudioLevels {
	bool valid = false;
	/// Overall RMS level, roughly 0..1.
	float level = 0.0f;
	/// Fast peak, useful for hits.
	float peak = 0.0f;
	float low = 0.0f;
	float mid = 0.0f;
	float high = 0.0f;

	float band(const std::string &id) const;
};

/// Everything a modulator can read.
struct ModContext {
	float time = 0.0f;
	float dt = 0.0f;
	AudioLevels audio;
};

/// Produces a 0..1 control value each frame.
class Modulator {
public:
	virtual ~Modulator() = default;
	virtual void configure(const ParamBag &params) { (void)params; }
	virtual float value(const ModContext &context) = 0;
};

using ModulatorRegistry = Registry<Modulator>;

/// Applies a set of modulation routes to a configuration.
///
/// Routes address parameters by id from `modulationTargets()`, so anything bound in the emission,
/// physics or render field tables can be driven without the engine knowing what it is.
class ModulationEngine {
public:
	ModulationEngine();
	~ModulationEngine();

	ModulationEngine(const ModulationEngine &) = delete;
	ModulationEngine &operator=(const ModulationEngine &) = delete;

	/// Rebuilds the modulators. Cheap to call when nothing changed: routes are compared first.
	void setRoutes(const std::vector<ModulationRoute> &routes);

	/// Writes `base` with every enabled route applied into `out`. Returns false when there is
	/// nothing to do, leaving `out` untouched. Copies the whole configuration, so prefer
	/// applyInPlace() for anything that runs every frame.
	bool apply(const EmitterConfig &base, const ModContext &context, EmitterConfig &out);

	/// Applies every enabled route directly to `live`, which must already equal `base` in every
	/// value no route drives - true of any configuration that started out as a copy of `base`
	/// and has only been written to by this engine since. Driven values are reset from `base`
	/// first, so routes never compound from one frame to the next. Nothing is allocated or
	/// copied. Returns false when there is nothing to do.
	bool applyInPlace(const EmitterConfig &base, const ModContext &context, EmitterConfig &live);

	bool empty() const { return routes_.empty(); }

	/// Last control value of each route, for meters in the UI.
	const std::vector<float> &values() const { return values_; }

private:
	enum class Table : uint8_t { None, Emission, Physics, Render };

	/// A route's target id resolved to a field-table entry, so the per-frame path does no string
	/// work at all.
	struct Target {
		Table table = Table::None;
		size_t field = 0;
	};

	struct RouteState {
		std::unique_ptr<Modulator> modulator;
		Target target;
		float smoothed = 0.0f;
		bool primed = false;
	};

	bool active() const;
	static Target resolveTarget(const std::string &id);

	std::vector<ModulationRoute> routes_;
	std::vector<RouteState> states_;
	std::vector<float> values_;
};

} // namespace atom
