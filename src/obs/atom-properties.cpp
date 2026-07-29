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

#include "atom-properties.hpp"
#include "atom-serialize.hpp"
#include "atom-source.hpp"
#include "ui/atom-ui.hpp"
#include "atom-core/atom-registry.hpp"

#include <string>
#include <vector>

namespace atom {

namespace {

/// Groups in the order they appear on the property page. Anything with an unlisted group id is
/// appended in first-seen order.
const std::vector<std::string> &groupOrder()
{
	static const std::vector<std::string> order = {"size", "location", "rate",  "motion",
						       "life", "endpoint", "offset"};
	return order;
}

const char *text(const std::string &key)
{
	return obs_module_text(key.c_str());
}

std::string combineRules(const std::string &base, const std::string &extra)
{
	if (base.empty())
		return extra;
	if (extra.empty())
		return base;
	return base + ";" + extra;
}

void addSourceList(obs_property_t *list, obs_source_t *self)
{
	obs_property_list_add_string(list, obs_module_text("Atom.Endpoint.Source.None"), "");

	struct Context {
		obs_property_t *list;
		obs_source_t *self;
	} context{list, self};

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			Context *ctx = static_cast<Context *>(param);
			if (source == ctx->self)
				return true;
			if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) == 0)
				return true;

			const char *name = obs_source_get_name(source);
			if (name && *name)
				obs_property_list_add_string(ctx->list, name, name);
			return true;
		},
		&context);
}

bool onModified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	(void)property;
	applyVisibility(props, settings);
	return true;
}

bool onOpenDesigner(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	ui::openDesigner(static_cast<obs_source_t *>(data));
	return false;
}

bool onResetEmitter(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	sourceResetRequested(static_cast<obs_source_t *>(data));
	return false;
}

void addProperty(obs_properties_t *props, const ParamSpec &spec, obs_source_t *self)
{
	obs_property_t *property = nullptr;

	switch (spec.type) {
	case ParamType::Bool:
		property = obs_properties_add_bool(props, spec.id.c_str(), text(spec.label));
		break;

	case ParamType::Int:
		property = obs_properties_add_int(props, spec.id.c_str(), text(spec.label), static_cast<int>(spec.min),
						  static_cast<int>(spec.max),
						  static_cast<int>(std::max(1.0, spec.step)));
		if (!spec.suffix.empty())
			obs_property_int_set_suffix(property, spec.suffix.c_str());
		break;

	case ParamType::Float:
		property = obs_properties_add_float_slider(props, spec.id.c_str(), text(spec.label), spec.min, spec.max,
							   spec.step);
		if (!spec.suffix.empty())
			obs_property_float_set_suffix(property, spec.suffix.c_str());
		break;

	case ParamType::Color:
		property = obs_properties_add_color_alpha(props, spec.id.c_str(), text(spec.label));
		break;

	case ParamType::Vec2: {
		const std::string label = text(spec.label);
		obs_property_t *x = obs_properties_add_float_slider(
			props, (spec.id + "_x").c_str(), (label + " X").c_str(), spec.min, spec.max, spec.step);
		obs_property_t *y = obs_properties_add_float_slider(
			props, (spec.id + "_y").c_str(), (label + " Y").c_str(), spec.min, spec.max, spec.step);
		obs_property_set_modified_callback(x, onModified);
		obs_property_set_modified_callback(y, onModified);
		if (!spec.description.empty()) {
			obs_property_set_long_description(x, text(spec.description));
			obs_property_set_long_description(y, text(spec.description));
		}
		return;
	}

	case ParamType::Enum: {
		property = obs_properties_add_list(props, spec.id.c_str(), text(spec.label), OBS_COMBO_TYPE_LIST,
						   OBS_COMBO_FORMAT_STRING);
		const std::vector<std::pair<std::string, std::string>> items =
			spec.enumRegistry.empty() ? spec.enumItems : registryEnumItems(spec.enumRegistry);
		for (const auto &item : items)
			obs_property_list_add_string(property, text(item.second), item.first.c_str());
		break;
	}

	case ParamType::SourceRef:
		property = obs_properties_add_list(props, spec.id.c_str(), text(spec.label), OBS_COMBO_TYPE_EDITABLE,
						   OBS_COMBO_FORMAT_STRING);
		addSourceList(property, self);
		break;

	case ParamType::Path:
		property = obs_properties_add_path(props, spec.id.c_str(), text(spec.label), OBS_PATH_FILE,
						   spec.filter.empty() ? nullptr : spec.filter.c_str(), nullptr);
		break;

	case ParamType::Text:
		property = obs_properties_add_text(props, spec.id.c_str(), text(spec.label), OBS_TEXT_DEFAULT);
		break;

	case ParamType::Gradient:
	case ParamType::Curve:
		// Edited in the Atom Designer; there is no OBS property widget for these.
		return;
	}

	if (!property)
		return;

	if (!spec.description.empty())
		obs_property_set_long_description(property, text(spec.description));

	obs_property_set_modified_callback(property, onModified);
}

/// Finds a property by id across the page and its groups.
obs_property_t *findProperty(obs_properties_t *props, const std::string &id)
{
	if (obs_property_t *direct = obs_properties_get(props, id.c_str()))
		return direct;

	for (obs_property_t *property = obs_properties_first(props); property != nullptr;
	     obs_property_next(&property)) {
		if (obs_property_get_type(property) != OBS_PROPERTY_GROUP)
			continue;
		obs_properties_t *group = obs_property_group_content(property);
		if (!group)
			continue;
		if (obs_property_t *found = findProperty(group, id))
			return found;
	}

	return nullptr;
}

void setVisible(obs_properties_t *props, const std::string &id, bool visible)
{
	if (obs_property_t *property = findProperty(props, id))
		obs_property_set_visible(property, visible);
}

} // namespace

const ParamSchema &fullEmitterSchema()
{
	static const ParamSchema schema = [] {
		registerBuiltinModules();

		ParamSchema combined = schemaOf(emissionFields());
		const ParamSchema physics = schemaOf(physicsFields());
		combined.insert(combined.end(), physics.begin(), physics.end());

		// Per-module parameters: every module contributes its own flat keys, shown only while
		// that module is selected.
		const auto appendModules = [&combined](const char *prefix, const char *registryName,
						       const char *selectorId, const char *group) {
			for (const auto &item : registryEnumItems(registryName)) {
				ParamSchema moduleSchema = serialize::prefixedSchema(
					registrySchema(registryName, item.first), prefix, item.first);
				const std::string rule = std::string(selectorId) + "=" + item.first;
				for (ParamSpec &spec : moduleSchema) {
					spec.visibleWhen = combineRules(rule, spec.visibleWhen);
					spec.group = group;
					combined.push_back(std::move(spec));
				}
			}
		};

		appendModules("shape", registries::kEmitterShape, "shape", "location");
		appendModules("falloff", registries::kFalloff, "falloff", "life");

		return combined;
	}();
	return schema;
}

void applyVisibility(obs_properties_t *props, obs_data_t *settings)
{
	const ParamSchema &schema = fullEmitterSchema();
	const ParamBag bag = serialize::readBag(settings, schema);

	for (const ParamSpec &spec : schema) {
		if (spec.visibleWhen.empty())
			continue;

		const bool visible = evaluateVisibility(spec.visibleWhen, bag);
		if (spec.type == ParamType::Vec2) {
			setVisible(props, spec.id + "_x", visible);
			setVisible(props, spec.id + "_y", visible);
		} else {
			setVisible(props, spec.id, visible);
		}
	}
}

obs_properties_t *buildEmitterProperties(obs_source_t *self)
{
	obs_properties_t *root = obs_properties_create();

	std::vector<std::pair<std::string, obs_properties_t *>> groups;
	const auto groupFor = [&groups](const std::string &id) -> obs_properties_t * {
		for (auto &entry : groups) {
			if (entry.first == id)
				return entry.second;
		}
		groups.emplace_back(id, obs_properties_create());
		return groups.back().second;
	};

	// Create the known groups up front so they keep a predictable order.
	for (const std::string &id : groupOrder())
		groupFor(id);

	for (const ParamSpec &spec : fullEmitterSchema())
		addProperty(spec.group.empty() ? root : groupFor(spec.group), spec, self);

	for (auto &entry : groups) {
		const std::string label = "Atom.Group." + entry.first;
		obs_properties_add_group(root, ("group_" + entry.first).c_str(), obs_module_text(label.c_str()),
					 OBS_GROUP_NORMAL, entry.second);
	}

	obs_properties_add_button2(root, "open_designer", obs_module_text("Atom.OpenDesigner"), onOpenDesigner, self);
	obs_properties_add_button2(root, "reset_emitter", obs_module_text("Atom.ResetEmitter"), onResetEmitter, self);

	return root;
}

} // namespace atom
