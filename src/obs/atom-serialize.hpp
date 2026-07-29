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

#include "atom-core/atom-config.hpp"

#include <obs-module.h>

namespace atom {
namespace serialize {

/// Reads/writes a single parameter. Vec2 is stored as `<id>_x` / `<id>_y` so it shows up as two
/// ordinary sliders in the OBS property page; gradients and curves become nested arrays.
void writeValue(obs_data_t *data, const ParamSpec &spec, const Value &value);
Value readValue(obs_data_t *data, const ParamSpec &spec);
void setDefault(obs_data_t *data, const ParamSpec &spec);

void writeBag(obs_data_t *data, const ParamSchema &schema, const ParamBag &bag);
ParamBag readBag(obs_data_t *data, const ParamSchema &schema);
void setDefaults(obs_data_t *data, const ParamSchema &schema);

/// Module parameters live under flat `<prefix>_<module id>_<param>` keys.
///
/// Flat keys mean the OBS property page can address them directly, and keying by module id means
/// switching a shape (or trail style, or fade path) and switching back does not lose settings.
std::string moduleParamKey(const std::string &prefix, const std::string &moduleId, const std::string &paramId);

/// Copy of `schema` with ids (and visibility expressions) rewritten to their flat keys.
ParamSchema prefixedSchema(const ParamSchema &schema, const std::string &prefix, const std::string &moduleId);

void writeModuleParams(obs_data_t *data, const std::string &prefix, const std::string &registryName,
		       const std::string &moduleId, const ParamBag &bag);
ParamBag readModuleParams(obs_data_t *data, const std::string &prefix, const std::string &registryName,
			  const std::string &moduleId);
/// Seeds defaults for every module in a registry.
void setModuleDefaults(obs_data_t *data, const std::string &prefix, const std::string &registryName);

/// Whole-config round trip against an OBS settings object.
void configToData(obs_data_t *data, const EmitterConfig &config);
EmitterConfig configFromData(obs_data_t *data);
void configDefaults(obs_data_t *data);

/// The design is also stored on its own so the designer window and preset files can share a format.
obs_data_t *designToData(const AtomDesign &design);
AtomDesign designFromData(obs_data_t *data);

obs_data_t *layerToData(const AtomLayer &layer);
AtomLayer layerFromData(obs_data_t *data);

} // namespace serialize
} // namespace atom
