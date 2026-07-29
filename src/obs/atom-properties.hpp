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
/// Nothing in the property page is generated from this any more (the designer owns editing), but
/// it stays the canonical description of an emitter's settings for serialization and for tooling
/// that needs to enumerate them.
const ParamSchema &fullEmitterSchema();

/// Builds the OBS property page for an Atom Emitter.
///
/// Editing happens in the Atom Designer, so the page itself is just the way in.
obs_properties_t *buildEmitterProperties(obs_source_t *self);

} // namespace atom
