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

#include "atom-serialize.hpp"
#include "atom-core/atom-registry.hpp"

namespace atom {
namespace serialize {

namespace {

std::string axisKey(const std::string &id, const char *axis)
{
	return id + "_" + axis;
}

void writeGradient(obs_data_t *data, const std::string &id, const Gradient &gradient)
{
	obs_data_array_t *array = obs_data_array_create();
	for (const GradientStop &stop : gradient.stops) {
		obs_data_t *item = obs_data_create();
		obs_data_set_double(item, "t", stop.t);
		obs_data_set_int(item, "color", stop.color.toRGBA());
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, id.c_str(), array);
	obs_data_array_release(array);
}

Gradient readGradient(obs_data_t *data, const std::string &id, const Gradient &fallback)
{
	obs_data_array_t *array = obs_data_get_array(data, id.c_str());
	if (!array)
		return fallback;

	Gradient gradient;
	const size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *item = obs_data_array_item(array, i);
		GradientStop stop;
		stop.t = static_cast<float>(obs_data_get_double(item, "t"));
		stop.color = Color::fromRGBA(static_cast<uint32_t>(obs_data_get_int(item, "color")));
		gradient.stops.push_back(stop);
		obs_data_release(item);
	}
	obs_data_array_release(array);

	if (gradient.stops.empty())
		return fallback;

	gradient.sort();
	return gradient;
}

void writeCurve(obs_data_t *data, const std::string &id, const Curve &curve)
{
	obs_data_array_t *array = obs_data_array_create();
	for (const CurvePoint &point : curve.points) {
		obs_data_t *item = obs_data_create();
		obs_data_set_double(item, "t", point.t);
		obs_data_set_double(item, "v", point.value);
		obs_data_array_push_back(array, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, id.c_str(), array);
	obs_data_array_release(array);
}

Curve readCurve(obs_data_t *data, const std::string &id, const Curve &fallback)
{
	obs_data_array_t *array = obs_data_get_array(data, id.c_str());
	if (!array)
		return fallback;

	Curve curve;
	const size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; ++i) {
		obs_data_t *item = obs_data_array_item(array, i);
		CurvePoint point;
		point.t = static_cast<float>(obs_data_get_double(item, "t"));
		point.value = static_cast<float>(obs_data_get_double(item, "v"));
		curve.points.push_back(point);
		obs_data_release(item);
	}
	obs_data_array_release(array);

	if (curve.points.empty())
		return fallback;

	curve.sort();
	return curve;
}

} // namespace

void writeValue(obs_data_t *data, const ParamSpec &spec, const Value &value)
{
	switch (spec.type) {
	case ParamType::Bool:
		obs_data_set_bool(data, spec.id.c_str(), detail::asBool(value));
		break;
	case ParamType::Int:
		obs_data_set_int(data, spec.id.c_str(), static_cast<long long>(detail::asDouble(value)));
		break;
	case ParamType::Float:
		obs_data_set_double(data, spec.id.c_str(), detail::asDouble(value));
		break;
	case ParamType::Color: {
		const Color *color = std::get_if<Color>(&value);
		obs_data_set_int(data, spec.id.c_str(), color ? color->toRGBA() : 0xFFFFFFFF);
		break;
	}
	case ParamType::Vec2: {
		const Vec2 *vec = std::get_if<Vec2>(&value);
		const Vec2 v = vec ? *vec : Vec2{};
		obs_data_set_double(data, axisKey(spec.id, "x").c_str(), v.x);
		obs_data_set_double(data, axisKey(spec.id, "y").c_str(), v.y);
		break;
	}
	case ParamType::Text:
	case ParamType::Enum:
	case ParamType::SourceRef:
	case ParamType::Path:
		obs_data_set_string(data, spec.id.c_str(), detail::asString(value).c_str());
		break;
	case ParamType::Gradient: {
		const Gradient *gradient = std::get_if<Gradient>(&value);
		writeGradient(data, spec.id, gradient ? *gradient : Gradient{});
		break;
	}
	case ParamType::Curve: {
		const Curve *curve = std::get_if<Curve>(&value);
		writeCurve(data, spec.id, curve ? *curve : Curve{});
		break;
	}
	}
}

Value readValue(obs_data_t *data, const ParamSpec &spec)
{
	switch (spec.type) {
	case ParamType::Bool:
		return Value(obs_data_get_bool(data, spec.id.c_str()));
	case ParamType::Int:
		return Value(static_cast<int64_t>(obs_data_get_int(data, spec.id.c_str())));
	case ParamType::Float:
		return Value(obs_data_get_double(data, spec.id.c_str()));
	case ParamType::Color:
		return Value(Color::fromRGBA(static_cast<uint32_t>(obs_data_get_int(data, spec.id.c_str()))));
	case ParamType::Vec2:
		return Value(Vec2{static_cast<float>(obs_data_get_double(data, axisKey(spec.id, "x").c_str())),
				  static_cast<float>(obs_data_get_double(data, axisKey(spec.id, "y").c_str()))});
	case ParamType::Text:
	case ParamType::Enum:
	case ParamType::SourceRef:
	case ParamType::Path: {
		const char *text = obs_data_get_string(data, spec.id.c_str());
		return Value(std::string(text ? text : ""));
	}
	case ParamType::Gradient: {
		const Gradient *fallback = std::get_if<Gradient>(&spec.defaultValue);
		return Value(readGradient(data, spec.id, fallback ? *fallback : Gradient{}));
	}
	case ParamType::Curve: {
		const Curve *fallback = std::get_if<Curve>(&spec.defaultValue);
		return Value(readCurve(data, spec.id, fallback ? *fallback : Curve{}));
	}
	}
	return spec.defaultValue;
}

void setDefault(obs_data_t *data, const ParamSpec &spec)
{
	switch (spec.type) {
	case ParamType::Bool:
		obs_data_set_default_bool(data, spec.id.c_str(), detail::asBool(spec.defaultValue));
		break;
	case ParamType::Int:
		obs_data_set_default_int(data, spec.id.c_str(),
					 static_cast<long long>(detail::asDouble(spec.defaultValue)));
		break;
	case ParamType::Float:
		obs_data_set_default_double(data, spec.id.c_str(), detail::asDouble(spec.defaultValue));
		break;
	case ParamType::Color: {
		const Color *color = std::get_if<Color>(&spec.defaultValue);
		obs_data_set_default_int(data, spec.id.c_str(), color ? color->toRGBA() : 0xFFFFFFFF);
		break;
	}
	case ParamType::Vec2: {
		const Vec2 *vec = std::get_if<Vec2>(&spec.defaultValue);
		const Vec2 v = vec ? *vec : Vec2{};
		obs_data_set_default_double(data, axisKey(spec.id, "x").c_str(), v.x);
		obs_data_set_default_double(data, axisKey(spec.id, "y").c_str(), v.y);
		break;
	}
	case ParamType::Text:
	case ParamType::Enum:
	case ParamType::SourceRef:
	case ParamType::Path:
		obs_data_set_default_string(data, spec.id.c_str(), detail::asString(spec.defaultValue).c_str());
		break;
	case ParamType::Gradient:
	case ParamType::Curve:
		// Arrays have no default API; readValue() falls back to the spec default when absent.
		break;
	}
}

void writeBag(obs_data_t *data, const ParamSchema &schema, const ParamBag &bag)
{
	for (const ParamSpec &spec : schema) {
		const Value *value = bag.find(spec.id);
		writeValue(data, spec, value ? *value : spec.defaultValue);
	}
}

ParamBag readBag(obs_data_t *data, const ParamSchema &schema)
{
	ParamBag bag;
	for (const ParamSpec &spec : schema)
		bag.set(spec.id, readValue(data, spec));
	return bag;
}

void setDefaults(obs_data_t *data, const ParamSchema &schema)
{
	for (const ParamSpec &spec : schema)
		setDefault(data, spec);
}

std::string moduleParamKey(const std::string &prefix, const std::string &moduleId, const std::string &paramId)
{
	return prefix + "_" + moduleId + "_" + paramId;
}

ParamSchema prefixedSchema(const ParamSchema &schema, const std::string &prefix, const std::string &moduleId)
{
	ParamSchema out;
	out.reserve(schema.size());
	for (const ParamSpec &spec : schema) {
		ParamSpec copy = spec;
		copy.id = moduleParamKey(prefix, moduleId, spec.id);
		if (!copy.visibleWhen.empty())
			copy.visibleWhen = moduleParamKey(prefix, moduleId, copy.visibleWhen);
		out.push_back(std::move(copy));
	}
	return out;
}

void writeModuleParams(obs_data_t *data, const std::string &prefix, const std::string &registryName,
		       const std::string &moduleId, const ParamBag &bag)
{
	const ParamSchema schema = registrySchema(registryName, moduleId);
	for (const ParamSpec &spec : schema) {
		ParamSpec keyed = spec;
		keyed.id = moduleParamKey(prefix, moduleId, spec.id);
		const Value *value = bag.find(spec.id);
		writeValue(data, keyed, value ? *value : spec.defaultValue);
	}
}

ParamBag readModuleParams(obs_data_t *data, const std::string &prefix, const std::string &registryName,
			  const std::string &moduleId)
{
	ParamBag bag;
	const ParamSchema schema = registrySchema(registryName, moduleId);
	for (const ParamSpec &spec : schema) {
		ParamSpec keyed = spec;
		keyed.id = moduleParamKey(prefix, moduleId, spec.id);
		bag.set(spec.id, readValue(data, keyed));
	}
	bag.applyDefaults(schema);
	return bag;
}

void setModuleDefaults(obs_data_t *data, const std::string &prefix, const std::string &registryName)
{
	for (const auto &item : registryEnumItems(registryName)) {
		const ParamSchema schema = registrySchema(registryName, item.first);
		for (const ParamSpec &spec : schema) {
			ParamSpec keyed = spec;
			keyed.id = moduleParamKey(prefix, item.first, spec.id);
			setDefault(data, keyed);
		}
	}
}

obs_data_t *layerToData(const AtomLayer &layer)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "id", layer.id.c_str());
	writeBag(data, schemaOf(layerFields()), toBag(layer, layerFields()));
	writeModuleParams(data, "sprite", registries::kSprite, layer.spriteId, layer.spriteParams);
	writeModuleParams(data, "trail", registries::kTrail, layer.trail.styleId, layer.trail.styleParams);
	writeModuleParams(data, "fade", registries::kFadePath, layer.fade.pathId, layer.fade.pathParams);
	return data;
}

AtomLayer layerFromData(obs_data_t *data)
{
	AtomLayer layer;
	const char *id = obs_data_get_string(data, "id");
	layer.id = (id && *id) ? id : makeLayerId();

	fromBag(layer, readBag(data, schemaOf(layerFields())), layerFields());
	layer.spriteParams = readModuleParams(data, "sprite", registries::kSprite, layer.spriteId);
	layer.trail.styleParams = readModuleParams(data, "trail", registries::kTrail, layer.trail.styleId);
	layer.fade.pathParams = readModuleParams(data, "fade", registries::kFadePath, layer.fade.pathId);
	return layer;
}

obs_data_t *designToData(const AtomDesign &design)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "name", design.name.c_str());
	obs_data_set_string(data, "preset_id", design.presetId.c_str());

	obs_data_array_t *layers = obs_data_array_create();
	for (const AtomLayer &layer : design.layers) {
		obs_data_t *item = layerToData(layer);
		obs_data_array_push_back(layers, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "layers", layers);
	obs_data_array_release(layers);
	return data;
}

AtomDesign designFromData(obs_data_t *data)
{
	if (!data)
		return AtomDesign::defaultDesign();

	AtomDesign design;
	const char *name = obs_data_get_string(data, "name");
	const char *presetId = obs_data_get_string(data, "preset_id");
	design.name = name ? name : "";
	design.presetId = presetId ? presetId : "";

	obs_data_array_t *layers = obs_data_get_array(data, "layers");
	if (layers) {
		const size_t count = obs_data_array_count(layers);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(layers, i);
			design.layers.push_back(layerFromData(item));
			obs_data_release(item);
		}
		obs_data_array_release(layers);
	}

	if (design.layers.empty())
		return AtomDesign::defaultDesign();

	return design;
}

void configToData(obs_data_t *data, const EmitterConfig &config)
{
	writeBag(data, schemaOf(emissionFields()), toBag(config.emission, emissionFields()));
	writeBag(data, schemaOf(physicsFields()), toBag(config.physics, physicsFields()));
	writeBag(data, schemaOf(renderFields()), toBag(config.render, renderFields()));
	writeBag(data, schemaOf(sceneFields()), toBag(config.scene, sceneFields()));
	writeBag(data, schemaOf(audioFields()), toBag(config.audio, audioFields()));

	obs_data_array_t *routes = obs_data_array_create();
	for (const ModulationRoute &route : config.modulation) {
		obs_data_t *item = obs_data_create();
		writeBag(item, schemaOf(routeFields()), toBag(route, routeFields()));
		writeModuleParams(item, "mod", registries::kModulator, route.modulatorId, route.modulatorParams);
		obs_data_array_push_back(routes, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "modulation", routes);
	obs_data_array_release(routes);

	writeModuleParams(data, "shape", registries::kEmitterShape, config.emission.shapeId,
			  config.emission.shapeParams);
	writeModuleParams(data, "falloff", registries::kFalloff, config.physics.falloffId,
			  config.physics.falloffParams);

	obs_data_array_t *behaviors = obs_data_array_create();
	for (const BehaviorInstance &instance : config.physics.extras) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "id", instance.id.c_str());
		obs_data_set_bool(item, "enabled", instance.enabled);
		obs_data_t *params = obs_data_create();
		writeBag(params, registrySchema(registries::kBehavior, instance.id), instance.params);
		obs_data_set_obj(item, "params", params);
		obs_data_release(params);
		obs_data_array_push_back(behaviors, item);
		obs_data_release(item);
	}
	obs_data_set_array(data, "behaviors", behaviors);
	obs_data_array_release(behaviors);

	obs_data_t *design = designToData(config.design);
	obs_data_set_obj(data, "design", design);
	obs_data_release(design);
}

EmitterConfig configFromData(obs_data_t *data)
{
	EmitterConfig config;
	if (!data)
		return config;

	fromBag(config.emission, readBag(data, schemaOf(emissionFields())), emissionFields());
	fromBag(config.physics, readBag(data, schemaOf(physicsFields())), physicsFields());
	fromBag(config.render, readBag(data, schemaOf(renderFields())), renderFields());
	fromBag(config.scene, readBag(data, schemaOf(sceneFields())), sceneFields());
	fromBag(config.audio, readBag(data, schemaOf(audioFields())), audioFields());

	obs_data_array_t *routes = obs_data_get_array(data, "modulation");
	if (routes) {
		const size_t count = obs_data_array_count(routes);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(routes, i);
			ModulationRoute route;
			fromBag(route, readBag(item, schemaOf(routeFields())), routeFields());
			route.modulatorParams =
				readModuleParams(item, "mod", registries::kModulator, route.modulatorId);
			config.modulation.push_back(std::move(route));
			obs_data_release(item);
		}
		obs_data_array_release(routes);
	}

	config.emission.shapeParams =
		readModuleParams(data, "shape", registries::kEmitterShape, config.emission.shapeId);
	config.physics.falloffParams =
		readModuleParams(data, "falloff", registries::kFalloff, config.physics.falloffId);

	obs_data_array_t *behaviors = obs_data_get_array(data, "behaviors");
	if (behaviors) {
		const size_t count = obs_data_array_count(behaviors);
		for (size_t i = 0; i < count; ++i) {
			obs_data_t *item = obs_data_array_item(behaviors, i);
			BehaviorInstance instance;
			const char *id = obs_data_get_string(item, "id");
			instance.id = id ? id : "";
			instance.enabled = obs_data_get_bool(item, "enabled");
			obs_data_t *params = obs_data_get_obj(item, "params");
			if (params) {
				instance.params = readBag(params, registrySchema(registries::kBehavior, instance.id));
				obs_data_release(params);
			}
			instance.params.applyDefaults(registrySchema(registries::kBehavior, instance.id));
			if (!instance.id.empty())
				config.physics.extras.push_back(std::move(instance));
			obs_data_release(item);
		}
		obs_data_array_release(behaviors);
	}

	obs_data_t *design = obs_data_get_obj(data, "design");
	config.design = designFromData(design);
	if (design)
		obs_data_release(design);

	return config;
}

void configDefaults(obs_data_t *data)
{
	setDefaults(data, schemaOf(emissionFields()));
	setDefaults(data, schemaOf(physicsFields()));
	setDefaults(data, schemaOf(renderFields()));
	setDefaults(data, schemaOf(sceneFields()));
	setDefaults(data, schemaOf(audioFields()));

	// Every module's parameters get their defaults too, so switching modules in the UI starts
	// from sensible values rather than zeroes.
	setModuleDefaults(data, "shape", registries::kEmitterShape);
	setModuleDefaults(data, "falloff", registries::kFalloff);

	obs_data_t *design = designToData(AtomDesign::defaultDesign());
	obs_data_set_default_obj(data, "design", design);
	obs_data_release(design);
}

} // namespace serialize
} // namespace atom
