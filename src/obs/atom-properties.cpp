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
#include "atom-core/atom-registry.hpp"
#include "ui/atom-ui.hpp"

#include <string>

namespace atom {

namespace {

std::string combineRules(const std::string &base, const std::string &extra)
{
	if (base.empty())
		return extra;
	if (extra.empty())
		return base;
	return base + ";" + extra;
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

} // namespace

const ParamSchema &fullEmitterSchema()
{
	static const ParamSchema schema = [] {
		registerBuiltinModules();

		ParamSchema combined = schemaOf(emissionFields());
		const ParamSchema physics = schemaOf(physicsFields());
		combined.insert(combined.end(), physics.begin(), physics.end());

		// Per-module parameters live under flat keys, one set per module, tagged with the
		// condition that selects that module.
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

obs_properties_t *buildEmitterProperties(obs_source_t *self)
{
	obs_properties_t *props = obs_properties_create();

	// Emitters are edited in the designer: it is the only place that can show a gradient, a
	// curve or a live preview, and splitting the controls across two windows only invites the
	// two to disagree.
	obs_property_t *info =
		obs_properties_add_text(props, "designer_hint", obs_module_text("Atom.PropertiesHint"), OBS_TEXT_INFO);
	obs_property_set_enabled(info, false);

	obs_properties_add_button2(props, "open_designer", obs_module_text("Atom.OpenDesigner"), onOpenDesigner, self);
	obs_properties_add_button2(props, "reset_emitter", obs_module_text("Atom.ResetEmitter"), onResetEmitter, self);

	return props;
}

} // namespace atom
