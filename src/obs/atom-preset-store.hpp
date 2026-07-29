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

#include "atom-core/atom-presets.hpp"

#include <string>
#include <vector>

namespace atom {

/// Built-in presets plus anything the user saved under the plugin's config directory.
class PresetStore {
public:
	static PresetStore &instance();

	/// Re-reads user presets from disk. Built-ins never change.
	void reload();

	const std::vector<AtomPreset> &presets() const { return presets_; }
	const AtomPreset *find(const std::string &id) const;

	/// Saves a user preset, replacing any existing one with the same id. Returns false and fills
	/// `error` when the file could not be written.
	bool save(const AtomPreset &preset, std::string *error = nullptr);

	/// Deletes a user preset. Built-ins cannot be removed.
	bool remove(const std::string &id);

	/// Directory user presets live in, created on demand.
	static std::string userPresetDirectory();

private:
	PresetStore() { reload(); }

	std::vector<AtomPreset> presets_;
};

} // namespace atom
