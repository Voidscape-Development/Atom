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

#include "atom-preset-store.hpp"
#include "atom-serialize.hpp"
#include "plugin-support.h"

#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <cctype>

namespace atom {

namespace {

std::string sanitizeId(const std::string &id)
{
	std::string clean;
	clean.reserve(id.size());
	for (const char c : id) {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
			clean += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		else if (c == ' ')
			clean += '_';
	}
	return clean.empty() ? std::string("preset") : clean;
}

obs_data_t *presetToData(const AtomPreset &preset)
{
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "id", preset.id.c_str());
	obs_data_set_string(data, "name", preset.name.c_str());
	obs_data_set_string(data, "category", preset.category.c_str());
	obs_data_set_string(data, "description", preset.description.c_str());
	obs_data_set_int(data, "format", 1);

	obs_data_t *settings = obs_data_create();
	serialize::configToData(settings, preset.config);
	obs_data_set_obj(data, "emitter", settings);
	obs_data_release(settings);

	return data;
}

bool presetFromData(obs_data_t *data, AtomPreset &out)
{
	if (!data)
		return false;

	const char *id = obs_data_get_string(data, "id");
	if (!id || !*id)
		return false;

	out.id = id;
	out.name = obs_data_get_string(data, "name");
	out.category = obs_data_get_string(data, "category");
	out.description = obs_data_get_string(data, "description");
	if (out.name.empty())
		out.name = out.id;
	if (out.category.empty())
		out.category = "user";

	obs_data_t *settings = obs_data_get_obj(data, "emitter");
	if (!settings)
		return false;

	out.config = serialize::configFromData(settings);
	obs_data_release(settings);
	return true;
}

} // namespace

PresetStore &PresetStore::instance()
{
	static PresetStore store;
	return store;
}

std::string PresetStore::userPresetDirectory()
{
	char *path = obs_module_config_path("presets");
	if (!path)
		return {};

	const std::string directory = path;
	bfree(path);

	os_mkdirs(directory.c_str());
	return directory;
}

void PresetStore::reload()
{
	presets_ = builtinPresets();

	const std::string directory = userPresetDirectory();
	if (directory.empty())
		return;

	os_dir_t *dir = os_opendir(directory.c_str());
	if (!dir)
		return;

	while (os_dirent *entry = os_readdir(dir)) {
		if (entry->directory)
			continue;

		const std::string name = entry->d_name;
		if (name.size() < 6 || name.compare(name.size() - 5, 5, ".json") != 0)
			continue;

		const std::string path = directory + "/" + name;
		obs_data_t *data = obs_data_create_from_json_file(path.c_str());
		if (!data) {
			obs_log(LOG_WARNING, "could not read preset '%s'", path.c_str());
			continue;
		}

		AtomPreset preset;
		if (presetFromData(data, preset)) {
			preset.builtin = false;
			preset.filePath = path;

			// A user preset with a built-in's id replaces it, which is how someone can
			// tweak a shipped preset and keep using the same name.
			const auto existing =
				std::find_if(presets_.begin(), presets_.end(),
					     [&preset](const AtomPreset &other) { return other.id == preset.id; });
			if (existing != presets_.end())
				*existing = preset;
			else
				presets_.push_back(preset);
		}

		obs_data_release(data);
	}

	os_closedir(dir);
}

const AtomPreset *PresetStore::find(const std::string &id) const
{
	for (const AtomPreset &preset : presets_) {
		if (preset.id == id)
			return &preset;
	}
	return nullptr;
}

bool PresetStore::save(const AtomPreset &preset, std::string *error)
{
	const std::string directory = userPresetDirectory();
	if (directory.empty()) {
		if (error)
			*error = "no writable preset directory";
		return false;
	}

	AtomPreset stored = preset;
	stored.builtin = false;
	if (stored.id.empty())
		stored.id = sanitizeId(stored.name);
	if (stored.category.empty())
		stored.category = "user";
	stored.filePath = directory + "/" + sanitizeId(stored.id) + ".json";

	obs_data_t *data = presetToData(stored);
	const bool ok = obs_data_save_json_safe(data, stored.filePath.c_str(), "tmp", "bak");
	obs_data_release(data);

	if (!ok) {
		if (error)
			*error = "could not write " + stored.filePath;
		return false;
	}

	reload();
	return true;
}

bool PresetStore::remove(const std::string &id)
{
	const AtomPreset *preset = find(id);
	if (!preset || preset->builtin || preset->filePath.empty())
		return false;

	const std::string path = preset->filePath;
	if (os_unlink(path.c_str()) != 0)
		return false;

	reload();
	return true;
}

} // namespace atom
