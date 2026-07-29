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

#include "atom-value.hpp"

#include <functional>

namespace atom {

/// Binds one struct member to a ParamSpec.
///
/// Field tables are the single source of truth for a config struct: the OBS property page, the
/// designer widgets and the settings serializer are all generated from them, so adding an option
/// means adding one binding.
template<typename T> struct FieldBinding {
	ParamSpec spec;
	std::function<Value(const T &)> get;
	std::function<void(T &, const Value &)> set;
};

template<typename T> using FieldTable = std::vector<FieldBinding<T>>;

template<typename T> ParamSchema schemaOf(const FieldTable<T> &fields)
{
	ParamSchema schema;
	schema.reserve(fields.size());
	for (const FieldBinding<T> &field : fields)
		schema.push_back(field.spec);
	return schema;
}

template<typename T> ParamBag toBag(const T &object, const FieldTable<T> &fields)
{
	ParamBag bag;
	for (const FieldBinding<T> &field : fields)
		bag.set(field.spec.id, field.get(object));
	return bag;
}

template<typename T> void fromBag(T &object, const ParamBag &bag, const FieldTable<T> &fields)
{
	for (const FieldBinding<T> &field : fields) {
		if (const Value *value = bag.find(field.spec.id))
			field.set(object, *value);
	}
}

namespace detail {

inline double asDouble(const Value &value, double fallback = 0.0)
{
	if (const double *d = std::get_if<double>(&value))
		return *d;
	if (const int64_t *i = std::get_if<int64_t>(&value))
		return static_cast<double>(*i);
	if (const bool *b = std::get_if<bool>(&value))
		return *b ? 1.0 : 0.0;
	return fallback;
}

inline bool asBool(const Value &value, bool fallback = false)
{
	if (const bool *b = std::get_if<bool>(&value))
		return *b;
	if (const int64_t *i = std::get_if<int64_t>(&value))
		return *i != 0;
	if (const double *d = std::get_if<double>(&value))
		return *d != 0.0;
	return fallback;
}

inline std::string asString(const Value &value, const std::string &fallback = {})
{
	if (const std::string *s = std::get_if<std::string>(&value))
		return *s;
	return fallback;
}

} // namespace detail

template<typename T, typename M>
FieldBinding<T> bindFloat(std::string id, std::string label, M T::*member, double min, double max, double step,
			  std::string group = {}, std::string suffix = {}, bool randomizable = false)
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Float;
	spec.min = min;
	spec.max = max;
	spec.step = step;
	spec.group = std::move(group);
	spec.suffix = std::move(suffix);
	spec.randomizable = randomizable;
	spec.defaultValue = static_cast<double>(T().*member);

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(static_cast<double>(object.*member)); },
		[member](T &object, const Value &value) {
			object.*member = static_cast<M>(detail::asDouble(value, static_cast<double>(object.*member)));
		},
	};
}

template<typename T, typename M>
FieldBinding<T> bindInt(std::string id, std::string label, M T::*member, double min, double max, double step,
			std::string group = {}, std::string suffix = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Int;
	spec.min = min;
	spec.max = max;
	spec.step = step;
	spec.group = std::move(group);
	spec.suffix = std::move(suffix);
	spec.defaultValue = static_cast<int64_t>(T().*member);

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(static_cast<int64_t>(object.*member)); },
		[member](T &object, const Value &value) {
			object.*member = static_cast<M>(detail::asDouble(value, static_cast<double>(object.*member)));
		},
	};
}

template<typename T>
FieldBinding<T> bindBool(std::string id, std::string label, bool T::*member, std::string group = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Bool;
	spec.group = std::move(group);
	spec.defaultValue = T().*member;

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(object.*member); },
		[member](T &object, const Value &value) { object.*member = detail::asBool(value, object.*member); },
	};
}

template<typename T>
FieldBinding<T> bindColor(std::string id, std::string label, Color T::*member, std::string group = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Color;
	spec.group = std::move(group);
	spec.defaultValue = T().*member;

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(object.*member); },
		[member](T &object, const Value &value) {
			if (const Color *c = std::get_if<Color>(&value))
				object.*member = *c;
			else if (const Gradient *g = std::get_if<Gradient>(&value))
				object.*member = g->sample(0.0f);
		},
	};
}

template<typename T>
FieldBinding<T> bindVec2(std::string id, std::string label, Vec2 T::*member, double min, double max, double step,
			 std::string group = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Vec2;
	spec.min = min;
	spec.max = max;
	spec.step = step;
	spec.group = std::move(group);
	spec.defaultValue = T().*member;

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(object.*member); },
		[member](T &object, const Value &value) {
			if (const Vec2 *v = std::get_if<Vec2>(&value))
				object.*member = *v;
		},
	};
}

template<typename T>
FieldBinding<T> bindGradient(std::string id, std::string label, Gradient T::*member, std::string group = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Gradient;
	spec.group = std::move(group);
	spec.defaultValue = T().*member;

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(object.*member); },
		[member](T &object, const Value &value) {
			if (const Gradient *g = std::get_if<Gradient>(&value))
				object.*member = *g;
			else if (const Color *c = std::get_if<Color>(&value))
				object.*member = Gradient::solid(*c);
		},
	};
}

template<typename T>
FieldBinding<T> bindCurve(std::string id, std::string label, Curve T::*member, std::string group = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Curve;
	spec.group = std::move(group);
	spec.defaultValue = T().*member;

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(object.*member); },
		[member](T &object, const Value &value) {
			if (const Curve *c = std::get_if<Curve>(&value))
				object.*member = *c;
			else if (const double *d = std::get_if<double>(&value))
				object.*member = Curve::constant(static_cast<float>(*d));
		},
	};
}

/// String-valued member holding an id from a fixed item list.
template<typename T>
FieldBinding<T> bindEnum(std::string id, std::string label, std::string T::*member,
			 std::vector<std::pair<std::string, std::string>> items, std::string group = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Enum;
	spec.enumItems = std::move(items);
	spec.group = std::move(group);
	spec.defaultValue = T().*member;

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(object.*member); },
		[member](T &object, const Value &value) { object.*member = detail::asString(value, object.*member); },
	};
}

/// String-valued member holding an id from a module registry, resolved lazily so modules can be
/// registered after the field table is built.
template<typename T>
FieldBinding<T> bindRegistryEnum(std::string id, std::string label, std::string T::*member, std::string registryName,
				 std::string group = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Enum;
	spec.enumRegistry = std::move(registryName);
	spec.group = std::move(group);
	spec.defaultValue = T().*member;

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(object.*member); },
		[member](T &object, const Value &value) { object.*member = detail::asString(value, object.*member); },
	};
}

template<typename T>
FieldBinding<T> bindText(std::string id, std::string label, std::string T::*member, ParamType type = ParamType::Text,
			 std::string group = {}, std::string filter = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = type;
	spec.group = std::move(group);
	spec.filter = std::move(filter);
	spec.defaultValue = T().*member;

	return FieldBinding<T>{
		std::move(spec),
		[member](const T &object) { return Value(object.*member); },
		[member](T &object, const Value &value) { object.*member = detail::asString(value, object.*member); },
	};
}

/// Escape hatch for values that are not a plain member, e.g. computed or clamped fields.
template<typename T>
FieldBinding<T> bindCustom(ParamSpec spec, std::function<Value(const T &)> get,
			   std::function<void(T &, const Value &)> set)
{
	return FieldBinding<T>{std::move(spec), std::move(get), std::move(set)};
}

/// Splices a nested struct's field table into a parent table, prefixing ids so the flattened
/// result stays serializable as a single key/value bag.
template<typename T, typename S>
void appendNested(FieldTable<T> &out, const FieldTable<S> &nested, S T::*member, const std::string &idPrefix,
		  const std::string &group = {})
{
	for (const FieldBinding<S> &field : nested) {
		FieldBinding<T> binding;
		binding.spec = field.spec;
		binding.spec.id = idPrefix + field.spec.id;
		if (!group.empty())
			binding.spec.group = group;
		if (!binding.spec.visibleWhen.empty())
			binding.spec.visibleWhen = idPrefix + binding.spec.visibleWhen;

		const auto get = field.get;
		const auto set = field.set;
		binding.get = [member, get](const T &object) {
			return get(object.*member);
		};
		binding.set = [member, set](T &object, const Value &value) {
			set(object.*member, value);
		};
		out.push_back(std::move(binding));
	}
}

/// Adds a visibility rule to an already-built binding, e.g. `visibleWhen(binding, "mode=point")`.
template<typename T> FieldBinding<T> visibleWhen(FieldBinding<T> binding, std::string expression)
{
	binding.spec.visibleWhen = std::move(expression);
	return binding;
}

template<typename T> FieldBinding<T> described(FieldBinding<T> binding, std::string description)
{
	binding.spec.description = std::move(description);
	return binding;
}

} // namespace atom
