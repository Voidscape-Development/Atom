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

/// Every parameter the emitter source stores in its settings: the emission and physics field
/// tables plus the flat, per-module parameters of every registered shape and falloff.
///
/// Both the property page and its visibility callback are generated from this, so registering a
/// new module is enough to make it appear in the UI.
const ParamSchema &fullEmitterSchema();

/// Builds the OBS property page for an Atom Emitter.
obs_properties_t *buildEmitterProperties(obs_source_t *self);

/// Re-applies every visibility rule. Used as the property modified callback.
void applyVisibility(obs_properties_t *props, obs_data_t *settings);

} // namespace atom
