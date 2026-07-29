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

#include "atom-value.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace atom {

/// Common header shared by every registered module description.
struct ModuleInfo {
	std::string id;
	/// Locale key for the display name.
	std::string label;
	std::string description;
	/// Optional grouping key used by the designer sidebar ("basic", "shape", "advanced", ...).
	std::string category;
	/// Parameters this module accepts. Drives OBS properties, designer widgets and serialization.
	ParamSchema params;
	/// Lower sorts first in pickers.
	int order = 100;
};

/// Generic id-keyed registry of module descriptions plus their factory.
///
/// This is the extension point of the plugin: emitter shapes, behaviours, trail styles, sprite
/// shapes and endpoint providers are all just registry entries, so a new one is a single
/// registration call with no changes to the source, the UI or serialization.
template<typename Interface> class Registry {
public:
	using Factory = std::function<std::unique_ptr<Interface>()>;

	struct Entry {
		ModuleInfo info;
		Factory factory;
	};

	static Registry &instance()
	{
		static Registry registry;
		return registry;
	}

	void add(ModuleInfo info, Factory factory)
	{
		for (Entry &existing : entries_) {
			if (existing.info.id == info.id) {
				existing = Entry{std::move(info), std::move(factory)};
				return;
			}
		}
		entries_.push_back(Entry{std::move(info), std::move(factory)});
		sorted_ = false;
	}

	const Entry *find(const std::string &id) const
	{
		for (const Entry &entry : entries_) {
			if (entry.info.id == id)
				return &entry;
		}
		return nullptr;
	}

	const ModuleInfo *info(const std::string &id) const
	{
		const Entry *entry = find(id);
		return entry ? &entry->info : nullptr;
	}

	ParamSchema schemaFor(const std::string &id) const
	{
		const Entry *entry = find(id);
		return entry ? entry->info.params : ParamSchema{};
	}

	std::unique_ptr<Interface> create(const std::string &id) const
	{
		const Entry *entry = find(id);
		return entry && entry->factory ? entry->factory() : nullptr;
	}

	/// All entries, ordered by ModuleInfo::order then id.
	const std::vector<Entry> &entries() const
	{
		if (!sorted_) {
			std::stable_sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) {
				if (a.info.order != b.info.order)
					return a.info.order < b.info.order;
				return a.info.id < b.info.id;
			});
			sorted_ = true;
		}
		return entries_;
	}

	std::string defaultId() const { return entries_.empty() ? std::string() : entries().front().info.id; }

	bool empty() const { return entries_.empty(); }

private:
	mutable std::vector<Entry> entries_;
	mutable bool sorted_ = true;
};

/// Registers the built-in modules. Safe to call more than once.
void registerBuiltinModules();

/// Enum items for a registry, addressed by the name used in ParamSpec::enumRegistry.
/// Returns an empty list for unknown registry names.
std::vector<std::pair<std::string, std::string>> registryEnumItems(const std::string &registryName);

/// Parameter schema of one entry of a named registry. Lets generic UI code stay free of any
/// knowledge about which registries exist.
ParamSchema registrySchema(const std::string &registryName, const std::string &entryId);

} // namespace atom
