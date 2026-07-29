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

double applyMode(const std::string &modeId, double current, float control, float amount)
{
	if (modeId == "multiply")
		return current * static_cast<double>(lerp(1.0f, amount, control));
	if (modeId == "replace")
		return static_cast<double>(lerp(static_cast<float>(current), amount, control));
	return current + static_cast<double>(amount * control);
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
		states_[i].modulator = ModulatorRegistry::instance().create(route.modulatorId);
		if (!states_[i].modulator)
			continue;

		ParamBag params = route.modulatorParams;
		params.applyDefaults(ModulatorRegistry::instance().schemaFor(route.modulatorId));
		states_[i].modulator->configure(params);
	}
}

bool ModulationEngine::apply(const EmitterConfig &base, const ModContext &context, EmitterConfig &out)
{
	if (routes_.empty())
		return false;

	bool any = false;
	for (const ModulationRoute &route : routes_) {
		if (route.enabled && !route.target.empty()) {
			any = true;
			break;
		}
	}
	if (!any)
		return false;

	out = base;

	// One bag per table; routes address parameters by id, so nothing here knows what a gravity
	// or a rate is.
	ParamBag emission = toBag(out.emission, emissionFields());
	ParamBag physics = toBag(out.physics, physicsFields());
	ParamBag render = toBag(out.render, renderFields());

	values_.assign(routes_.size(), 0.0f);

	for (size_t i = 0; i < routes_.size(); ++i) {
		if (i >= states_.size())
			break;

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

		std::string table;
		std::string param;
		if (!splitTarget(route.target, table, param))
			continue;

		ParamBag *bag = table == "emission" ? &emission : (table == "physics" ? &physics : &render);
		const Value *current = bag->find(param);
		if (!current)
			continue;

		const double updated = applyMode(route.modeId, detail::asDouble(*current), control, route.amount);
		if (std::get_if<int64_t>(current))
			bag->set(param, static_cast<int64_t>(updated));
		else
			bag->set(param, updated);
	}

	fromBag(out.emission, emission, emissionFields());
	fromBag(out.physics, physics, physicsFields());
	fromBag(out.render, render, renderFields());
	return true;
}

} // namespace atom
