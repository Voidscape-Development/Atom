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

#include "atom-modulation.hpp"

namespace atom {

namespace {

/// Splits a target id such as "physics.gravity" into its table and parameter parts.
bool splitTarget(const std::string &target, std::string &table, std::string &param)
{
	const size_t dot = target.find('.');
	if (dot == std::string::npos)
		return false;
	table = target.substr(0, dot);
	param = target.substr(dot + 1);
	return true;
}

template<typename T> bool findField(const FieldTable<T> &fields, const std::string &id, size_t &index)
{
	for (size_t i = 0; i < fields.size(); ++i) {
		if (fields[i].spec.id == id) {
			index = i;
			return true;
		}
	}
	return false;
}

bool sameRoutes(const std::vector<ModulationRoute> &a, const std::vector<ModulationRoute> &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].modulatorId != b[i].modulatorId || a[i].target != b[i].target || a[i].modeId != b[i].modeId ||
		    a[i].enabled != b[i].enabled || a[i].modulatorParams.values() != b[i].modulatorParams.values())
			return false;
	}
	return true;
}

} // namespace

float AudioLevels::band(const std::string &id) const
{
	if (id == "peak")
		return peak;
	if (id == "low")
		return low;
	if (id == "mid")
		return mid;
	if (id == "high")
		return high;
	return level;
}

ModulationEngine::ModulationEngine() = default;
ModulationEngine::~ModulationEngine() = default;

ModulationEngine::Target ModulationEngine::resolveTarget(const std::string &id)
{
	Target target;
	std::string table;
	std::string param;
	if (!splitTarget(id, table, param))
		return target;

	// Anything that is not emission or physics is looked up in the render table.
	if (table == "emission") {
		if (findField(emissionFields(), param, target.field))
			target.table = Table::Emission;
	} else if (table == "physics") {
		if (findField(physicsFields(), param, target.field))
			target.table = Table::Physics;
	} else if (findField(renderFields(), param, target.field)) {
		target.table = Table::Render;
	}
	return target;
}

void ModulationEngine::setRoutes(const std::vector<ModulationRoute> &routes)
{
	// Amount and smoothing can change without rebuilding anything, which matters because the
	// designer writes the whole config back on every slider move.
	const bool rebuild = !sameRoutes(routes_, routes);
	routes_ = routes;
	if (!rebuild)
		return;

	states_.clear();
	states_.resize(routes_.size());
	values_.assign(routes_.size(), 0.0f);

	for (size_t i = 0; i < routes_.size(); ++i) {
		const ModulationRoute &route = routes_[i];
		states_[i].target = resolveTarget(route.target);
		states_[i].modulator = ModulatorRegistry::instance().create(route.modulatorId);
		if (!states_[i].modulator)
			continue;
		ParamBag params = route.modulatorParams;
		params.applyDefaults(ModulatorRegistry::instance().schemaFor(route.modulatorId));
		states_[i].modulator->configure(params);
	}
}

bool ModulationEngine::active() const
{
	for (const ModulationRoute &route : routes_) {
		if (route.enabled && !route.target.empty())
			return true;
	}
	return false;
}

bool ModulationEngine::apply(const EmitterConfig &base, const ModContext &context, EmitterConfig &out)
{
	if (!active())
		return false;

	out = base;
	return applyInPlace(base, context, out);
}

bool ModulationEngine::applyInPlace(const EmitterConfig &base, const ModContext &context, EmitterConfig &live)
{
	if (!active())
		return false;

	const auto read = [](const EmitterConfig &config, const Target &target) -> Value {
		switch (target.table) {
		case Table::Emission:
			return emissionFields()[target.field].get(config.emission);
		case Table::Physics:
			return physicsFields()[target.field].get(config.physics);
		case Table::Render:
			return renderFields()[target.field].get(config.render);
		case Table::None:
			break;
		}
		return Value(0.0);
	};
	const auto write = [](EmitterConfig &config, const Target &target, const Value &value) {
		switch (target.table) {
		case Table::Emission:
			emissionFields()[target.field].set(config.emission, value);
			break;
		case Table::Physics:
			physicsFields()[target.field].set(config.physics, value);
			break;
		case Table::Render:
			renderFields()[target.field].set(config.render, value);
			break;
		case Table::None:
			break;
		}
	};

	const size_t count = std::min(routes_.size(), states_.size());

	// Back to the saved values first, so several routes on one parameter stack on the baseline
	// rather than on last frame's result.
	for (size_t i = 0; i < count; ++i) {
		const Target &target = states_[i].target;
		if (target.table != Table::None)
			write(live, target, read(base, target));
	}

	values_.assign(routes_.size(), 0.0f);

	for (size_t i = 0; i < count; ++i) {
		const ModulationRoute &route = routes_[i];
		RouteState &state = states_[i];
		if (!route.enabled || !state.modulator)
			continue;

		float control = saturate(state.modulator->value(context));
		if (route.smoothing > 1e-4f && context.dt > 0.0f) {
			// Exponential approach, so the same smoothing feels the same at any frame rate.
			const float alpha = 1.0f - std::exp(-context.dt / route.smoothing);
			state.smoothed = state.primed ? lerp(state.smoothed, control, alpha) : control;
		} else {
			state.smoothed = control;
		}
		state.primed = true;
		control = state.smoothed;
		values_[i] = control;

		if (state.target.table == Table::None)
			continue;

		const Value current = read(live, state.target);
		const double value = detail::asDouble(current);
		double updated = value + static_cast<double>(route.amount * control);
		if (route.modeId == "multiply")
			updated = value * static_cast<double>(lerp(1.0f, route.amount, control));
		else if (route.modeId == "replace")
			updated = static_cast<double>(lerp(static_cast<float>(value), route.amount, control));

		if (std::get_if<int64_t>(&current))
			write(live, state.target, Value(static_cast<int64_t>(updated)));
		else
			write(live, state.target, Value(updated));
	}

	return true;
}

} // namespace atom
