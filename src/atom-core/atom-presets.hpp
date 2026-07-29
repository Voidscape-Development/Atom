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

#include "atom-config.hpp"

namespace atom {

/// A named, ready-to-use emitter configuration.
///
/// Built-ins are defined in code; anything the user saves is written to the plugin's config
/// directory in the same shape, so both kinds are interchangeable in the designer.
struct AtomPreset {
	std::string id;
	std::string name;
	/// Category id used by the designer sidebar, e.g. "fire", "smoke", "magic".
	std::string category;
	std::string description;

	EmitterConfig config;

	bool builtin = false;
	/// Where a user preset came from; empty for built-ins.
	std::string filePath;
};

/// Categories in sidebar order, as {id, locale key}.
const std::vector<std::pair<std::string, std::string>> &presetCategories();

/// The presets shipped with the plugin.
const std::vector<AtomPreset> &builtinPresets();

} // namespace atom
