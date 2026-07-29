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

namespace atom {

/// Registers Atom's obs-websocket vendor and its requests.
///
/// obs-websocket exposes vendor registration through OBS' global proc handler, so this needs no
/// build dependency on obs-websocket: if it is not loaded, registration simply reports that and
/// hotkeys plus the per-source proc handler still work.
///
/// Requests (vendor "atom", all take "source" and return "ok"):
///   burst        {source, count?}   emits a burst; omit count for the configured burst size
///   reset        {source}           clears every live atom
///   set_emitting {source, emitting} starts or stops emission
///   status       {source}           returns {atoms, emitting}
void registerTriggerApi();

} // namespace atom
