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

/// Source id of the Atom Emitter.
constexpr const char *kEmitterSourceId = "atom_emitter";

void registerEmitterSource();

/// Clears every live atom on a source and restarts its emission schedule.
void sourceResetRequested(obs_source_t *source);

/// Reads the emitter configuration currently stored on a source.
EmitterConfig configOfSource(obs_source_t *source);

/// Writes a whole configuration back to a live source, which reconfigures it immediately.
void applyConfigToSource(obs_source_t *source, const EmitterConfig &config);

} // namespace atom
