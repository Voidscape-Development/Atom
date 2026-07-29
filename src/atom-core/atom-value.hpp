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

#include "atom-math.hpp"

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace atom {

/// A colour stop on a lifetime gradient. `t` is normalized atom age (0 = spawn, 1 = death).
struct GradientStop {
	float t = 0.0f;
	Color color;

	bool operator==(const GradientStop &o) const { return t == o.t && color == o.color; }
};

/// Multi-stop colour ramp evaluated over an atom's lifetime.
struct Gradient {
	std::vector<GradientStop> stops;

	static Gradient solid(const Color &color) { return Gradient{{{0.0f, color}, {1.0f, color}}}; }

	Color sample(float t) const;

	/// Keeps stops ordered by `t`; call after any edit that can reorder them.
	void sort();

	bool operator==(const Gradient &o) const { return stops == o.stops; }
	bool operator!=(const Gradient &o) const { return !(*this == o); }
};

/// A point on an editable response curve.
struct CurvePoint {
	float t = 0.0f;
	float value = 0.0f;

	bool operator==(const CurvePoint &o) const { return t == o.t && value == o.value; }
};

/// Piecewise-linear curve used for size-over-life, fade paths, emission-rate shaping, etc.
/// Deliberately generic: any future module can take a Curve parameter without new plumbing.
struct Curve {
	std::vector<CurvePoint> points;

	static Curve constant(float value) { return Curve{{{0.0f, value}, {1.0f, value}}}; }
	static Curve linear(float from, float to) { return Curve{{{0.0f, from}, {1.0f, to}}}; }

	float sample(float t) const;
	void sort();

	bool operator==(const Curve &o) const { return points == o.points; }
	bool operator!=(const Curve &o) const { return !(*this == o); }
};

/// Every value an Atom module can be configured with. Adding a new kind here is the only change
/// needed to make it available to the OBS property page, the designer UI and serialization.
using Value = std::variant<bool, int64_t, double, Color, Vec2, std::string, Gradient, Curve>;

enum class ParamType {
	Bool,
	Int,
	Float,
	Color,
	Vec2,
	Text,
	/// String id chosen from a fixed item list or from a module registry.
	Enum,
	Gradient,
	Curve,
	/// String naming an OBS source; resolved by the host, never by the core.
	SourceRef,
	/// Filesystem path; `filter` holds the file dialog filter.
	Path,
};

/// Declarative description of one configurable parameter.
///
/// Modules describe themselves with these instead of hand-writing UI, so a new emitter shape or
/// behaviour automatically gets an OBS property widget, a designer widget and serialization.
struct ParamSpec {
	std::string id;
	/// Locale key for the display name (falls back to the raw string if the key is missing).
	std::string label;
	std::string description;
	ParamType type = ParamType::Float;
	Value defaultValue = 0.0;

	double min = 0.0;
	double max = 1.0;
	double step = 0.01;
	std::string suffix;

	/// Explicit enum items as {id, locale key}. Ignored when `enumRegistry` is set.
	std::vector<std::pair<std::string, std::string>> enumItems;
	/// Name of a module registry whose entries populate this enum ("emitter_shape", "behavior", ...).
	std::string enumRegistry;

	/// Optional grouping key, used to lay parameters out in collapsible sections.
	std::string group;
	/// Simple visibility rule of the form "<param_id>=<value>" or "<param_id>!=<value>".
	/// Empty means always visible.
	std::string visibleWhen;
	std::string filter;

	/// True when the parameter is meaningfully randomizable per atom, which lets the Offset
	/// section offer a variance slider for it without hard-coding a list.
	bool randomizable = false;
};

using ParamSchema = std::vector<ParamSpec>;

/// Type-erased bag of parameter values keyed by ParamSpec::id.
class ParamBag {
public:
	bool has(const std::string &id) const { return values_.find(id) != values_.end(); }

	void set(const std::string &id, Value value) { values_[id] = std::move(value); }

	const Value *find(const std::string &id) const;

	bool getBool(const std::string &id, bool fallback = false) const;
	int64_t getInt(const std::string &id, int64_t fallback = 0) const;
	double getDouble(const std::string &id, double fallback = 0.0) const;
	float getFloat(const std::string &id, float fallback = 0.0f) const
	{
		return static_cast<float>(getDouble(id, fallback));
	}
	Color getColor(const std::string &id, const Color &fallback = Color()) const;
	Vec2 getVec2(const std::string &id, const Vec2 &fallback = Vec2()) const;
	std::string getString(const std::string &id, const std::string &fallback = {}) const;
	Gradient getGradient(const std::string &id, const Gradient &fallback = {}) const;
	Curve getCurve(const std::string &id, const Curve &fallback = {}) const;

	/// Inserts defaults for any spec not already present. Existing values are left untouched so
	/// user edits survive schema changes.
	void applyDefaults(const ParamSchema &schema);

	/// Drops values whose id is not in `schema`. Use when compacting saved data.
	void prune(const ParamSchema &schema);

	const std::map<std::string, Value> &values() const { return values_; }
	void clear() { values_.clear(); }
	bool empty() const { return values_.empty(); }

private:
	std::map<std::string, Value> values_;
};

/// Evaluates a ParamSpec::visibleWhen expression against a bag. Unknown ids evaluate to visible.
bool evaluateVisibility(const std::string &expression, const ParamBag &bag);

/// Finds a spec by id, or nullptr.
const ParamSpec *findSpec(const ParamSchema &schema, const std::string &id);

} // namespace atom
