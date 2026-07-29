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

#include <obs.h>

namespace atom {
namespace ui {

/// Opens (or raises) the Atom Designer for a source. Falls back to a log message in builds
/// configured without Qt.
void openDesigner(obs_source_t *source);

/// Closes every open designer window. Called on module unload.
void shutdown();

} // namespace ui
} // namespace atom
