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

#include "atom-value.hpp"

namespace atom {

Color Gradient::sample(float t) const
{
	if (stops.empty())
		return Color();
	if (stops.size() == 1)
		return stops.front().color;

	t = saturate(t);
	if (t <= stops.front().t)
		return stops.front().color;
	if (t >= stops.back().t)
		return stops.back().color;

	for (size_t i = 1; i < stops.size(); ++i) {
		const GradientStop &b = stops[i];
		if (t > b.t)
			continue;

		const GradientStop &a = stops[i - 1];
		const float span = b.t - a.t;
		const float local = span > 1e-6f ? (t - a.t) / span : 0.0f;
		return lerp(a.color, b.color, local);
	}

	return stops.back().color;
}

void Gradient::sort()
{
	std::stable_sort(stops.begin(), stops.end(),
			 [](const GradientStop &a, const GradientStop &b) { return a.t < b.t; });
}

float Curve::sample(float t) const
{
	if (points.empty())
		return 0.0f;
	if (points.size() == 1)
		return points.front().value;

	t = saturate(t);
	if (t <= points.front().t)
		return points.front().value;
	if (t >= points.back().t)
		return points.back().value;

	for (size_t i = 1; i < points.size(); ++i) {
		const CurvePoint &b = points[i];
		if (t > b.t)
			continue;

		const CurvePoint &a = points[i - 1];
		const float span = b.t - a.t;
		const float local = span > 1e-6f ? (t - a.t) / span : 0.0f;
		return lerp(a.value, b.value, local);
	}

	return points.back().value;
}

void Curve::sort()
{
	std::stable_sort(points.begin(), points.end(),
			 [](const CurvePoint &a, const CurvePoint &b) { return a.t < b.t; });
}

const Value *ParamBag::find(const std::string &id) const
{
	const auto it = values_.find(id);
	return it == values_.end() ? nullptr : &it->second;
}

bool ParamBag::getBool(const std::string &id, bool fallback) const
{
	const Value *value = find(id);
	if (!value)
		return fallback;
	if (const bool *b = std::get_if<bool>(value))
		return *b;
	if (const int64_t *i = std::get_if<int64_t>(value))
		return *i != 0;
	if (const double *d = std::get_if<double>(value))
		return *d != 0.0;
	return fallback;
}

int64_t ParamBag::getInt(const std::string &id, int64_t fallback) const
{
	const Value *value = find(id);
	if (!value)
		return fallback;
	if (const int64_t *i = std::get_if<int64_t>(value))
		return *i;
	if (const double *d = std::get_if<double>(value))
		return static_cast<int64_t>(*d);
	if (const bool *b = std::get_if<bool>(value))
		return *b ? 1 : 0;
	return fallback;
}

double ParamBag::getDouble(const std::string &id, double fallback) const
{
	const Value *value = find(id);
	if (!value)
		return fallback;
	if (const double *d = std::get_if<double>(value))
		return *d;
	if (const int64_t *i = std::get_if<int64_t>(value))
		return static_cast<double>(*i);
	if (const bool *b = std::get_if<bool>(value))
		return *b ? 1.0 : 0.0;
	return fallback;
}

Color ParamBag::getColor(const std::string &id, const Color &fallback) const
{
	const Value *value = find(id);
	if (!value)
		return fallback;
	if (const Color *c = std::get_if<Color>(value))
		return *c;
	if (const Gradient *g = std::get_if<Gradient>(value))
		return g->sample(0.0f);
	return fallback;
}

Vec2 ParamBag::getVec2(const std::string &id, const Vec2 &fallback) const
{
	const Value *value = find(id);
	if (!value)
		return fallback;
	if (const Vec2 *v = std::get_if<Vec2>(value))
		return *v;
	return fallback;
}

std::string ParamBag::getString(const std::string &id, const std::string &fallback) const
{
	const Value *value = find(id);
	if (!value)
		return fallback;
	if (const std::string *s = std::get_if<std::string>(value))
		return *s;
	return fallback;
}

Gradient ParamBag::getGradient(const std::string &id, const Gradient &fallback) const
{
	const Value *value = find(id);
	if (!value)
		return fallback;
	if (const Gradient *g = std::get_if<Gradient>(value))
		return *g;
	if (const Color *c = std::get_if<Color>(value))
		return Gradient::solid(*c);
	return fallback;
}

Curve ParamBag::getCurve(const std::string &id, const Curve &fallback) const
{
	const Value *value = find(id);
	if (!value)
		return fallback;
	if (const Curve *c = std::get_if<Curve>(value))
		return *c;
	if (const double *d = std::get_if<double>(value))
		return Curve::constant(static_cast<float>(*d));
	return fallback;
}

void ParamBag::applyDefaults(const ParamSchema &schema)
{
	for (const ParamSpec &spec : schema) {
		if (values_.find(spec.id) == values_.end())
			values_[spec.id] = spec.defaultValue;
	}
}

void ParamBag::prune(const ParamSchema &schema)
{
	for (auto it = values_.begin(); it != values_.end();) {
		if (findSpec(schema, it->first))
			++it;
		else
			it = values_.erase(it);
	}
}

const ParamSpec *findSpec(const ParamSchema &schema, const std::string &id)
{
	for (const ParamSpec &spec : schema) {
		if (spec.id == id)
			return &spec;
	}
	return nullptr;
}

namespace {

bool evaluateClause(const std::string &expression, const ParamBag &bag)
{
	const size_t opPos = expression.find_first_of("=!");
	if (opPos == std::string::npos)
		return true;

	const bool negated = expression[opPos] == '!';
	const size_t valuePos = expression.find('=', opPos);
	if (valuePos == std::string::npos)
		return true;

	const std::string id = expression.substr(0, opPos);
	const std::string expected = expression.substr(valuePos + 1);

	const Value *value = bag.find(id);
	if (!value)
		return true;

	std::string actual;
	if (const std::string *s = std::get_if<std::string>(value)) {
		actual = *s;
	} else if (const bool *b = std::get_if<bool>(value)) {
		actual = *b ? "true" : "false";
	} else if (const int64_t *i = std::get_if<int64_t>(value)) {
		actual = std::to_string(*i);
	} else if (const double *d = std::get_if<double>(value)) {
		actual = std::to_string(static_cast<int64_t>(*d));
	} else {
		return true;
	}

	const bool matches = actual == expected;
	return negated ? !matches : matches;
}

} // namespace

bool evaluateVisibility(const std::string &expression, const ParamBag &bag)
{
	if (expression.empty())
		return true;

	// Clauses are separated by ';' and combined with AND.
	size_t start = 0;
	while (start <= expression.size()) {
		const size_t end = expression.find(';', start);
		const std::string clause =
			expression.substr(start, end == std::string::npos ? std::string::npos : end - start);
		if (!clause.empty() && !evaluateClause(clause, bag))
			return false;
		if (end == std::string::npos)
			break;
		start = end + 1;
	}

	return true;
}

} // namespace atom
