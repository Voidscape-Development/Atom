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

#include "atom-source.hpp"
#include "atom-properties.hpp"
#include "atom-renderer.hpp"
#include "atom-serialize.hpp"
#include "atom-core/atom-system.hpp"
#include "plugin-support.h"

#include <graphics/matrix4.h>
#include <graphics/vec3.h>

#include <map>
#include <mutex>

namespace atom {

namespace {

struct EmitterSource {
	obs_source_t *source = nullptr;
	AtomSystem system;
	AtomRenderer renderer;

	/// Guards `system` between the UI thread (settings updates) and the video thread.
	std::mutex mutex;
};

std::mutex g_instancesMutex;
std::map<obs_source_t *, EmitterSource *> g_instances;

EmitterSource *lookup(obs_source_t *source)
{
	std::lock_guard<std::mutex> lock(g_instancesMutex);
	const auto it = g_instances.find(source);
	return it == g_instances.end() ? nullptr : it->second;
}

/// Point inside a scene item's bounding box for a given anchor id, in unit coordinates.
Vec2 anchorPoint(const std::string &anchorId, Random &random)
{
	if (anchorId == "top_left")
		return {0.0f, 0.0f};
	if (anchorId == "top_right")
		return {1.0f, 0.0f};
	if (anchorId == "bottom_left")
		return {0.0f, 1.0f};
	if (anchorId == "bottom_right")
		return {1.0f, 1.0f};
	if (anchorId == "random")
		return {random.next(), random.next()};
	return {0.5f, 0.5f};
}

struct TargetSearch {
	obs_source_t *self = nullptr;
	const char *selfName = nullptr;
	const char *targetName = nullptr;
	std::string anchorId;
	Vec2 result;
	bool found = false;
};

/// Converts a scene item's position into the emitter's local pixel space.
///
/// Both items are looked up in the same scene, so the emitter follows the target even when either
/// one is moved, scaled or rotated.
bool resolveInScene(obs_scene_t *scene, TargetSearch &search)
{
	obs_sceneitem_t *selfItem = obs_scene_find_source_recursive(scene, search.selfName);
	obs_sceneitem_t *targetItem = obs_scene_find_source_recursive(scene, search.targetName);
	if (!selfItem || !targetItem)
		return false;

	matrix4 selfBox;
	matrix4 targetBox;
	obs_sceneitem_get_box_transform(selfItem, &selfBox);
	obs_sceneitem_get_box_transform(targetItem, &targetBox);

	matrix4 selfInverse;
	if (!matrix4_inv(&selfInverse, &selfBox))
		return false;

	static Random random(0x51F3u);
	const Vec2 anchor = anchorPoint(search.anchorId, random);

	vec3 point;
	vec3_set(&point, anchor.x, anchor.y, 0.0f);
	vec3_transform(&point, &point, &targetBox);
	vec3_transform(&point, &point, &selfInverse);

	// The box transform maps the source's unit square, so the result is normalized to the
	// emitter's own size.
	search.result = {point.x, point.y};
	search.found = true;
	return true;
}

bool resolveSceneTarget(obs_source_t *self, const std::string &targetName, const std::string &anchorId, Vec2 &out)
{
	if (targetName.empty())
		return false;

	const char *selfName = obs_source_get_name(self);
	if (!selfName)
		return false;

	TargetSearch search;
	search.self = self;
	search.selfName = selfName;
	search.targetName = targetName.c_str();
	search.anchorId = anchorId;

	obs_enum_scenes(
		[](void *param, obs_source_t *sceneSource) {
			TargetSearch *ctx = static_cast<TargetSearch *>(param);
			obs_scene_t *scene = obs_scene_from_source(sceneSource);
			if (!scene)
				return true;
			return !resolveInScene(scene, *ctx);
		},
		&search);

	if (!search.found)
		return false;

	out = search.result;
	return true;
}

const char *emitterGetName(void *)
{
	return obs_module_text("Atom.Emitter");
}

void *emitterCreate(obs_data_t *settings, obs_source_t *source)
{
	registerBuiltinModules();

	EmitterSource *emitter = new EmitterSource();
	emitter->source = source;
	emitter->system.configure(serialize::configFromData(settings));

	{
		std::lock_guard<std::mutex> lock(g_instancesMutex);
		g_instances[source] = emitter;
	}

	return emitter;
}

void emitterDestroy(void *data)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	if (!emitter)
		return;

	{
		std::lock_guard<std::mutex> lock(g_instancesMutex);
		g_instances.erase(emitter->source);
	}

	delete emitter;
}

void emitterUpdate(void *data, obs_data_t *settings)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	const EmitterConfig config = serialize::configFromData(settings);

	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.configure(config);
}

void emitterDefaults(obs_data_t *settings)
{
	registerBuiltinModules();
	serialize::configDefaults(settings);
}

obs_properties_t *emitterProperties(void *data)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	return buildEmitterProperties(emitter ? emitter->source : nullptr);
}

uint32_t emitterWidth(void *data)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	std::lock_guard<std::mutex> lock(emitter->mutex);
	return emitter->system.config().emission.width;
}

uint32_t emitterHeight(void *data)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	std::lock_guard<std::mutex> lock(emitter->mutex);
	return emitter->system.config().emission.height;
}

void emitterTick(void *data, float seconds)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);

	// The scene lookup walks OBS' source list, so it happens between our own locks rather than
	// under them.
	bool needsTarget = false;
	std::string targetName;
	std::string anchorId;
	float width = 0.0f;
	float height = 0.0f;
	{
		std::lock_guard<std::mutex> lock(emitter->mutex);
		needsTarget = emitter->system.needsHostTarget();
		if (needsTarget) {
			const EndpointConfig &endpoint = emitter->system.config().physics.endpoint;
			targetName = endpoint.sourceName;
			anchorId = endpoint.anchorId;
			width = emitter->system.width();
			height = emitter->system.height();
		}
	}

	SimContext context;
	if (needsTarget) {
		Vec2 normalized;
		if (resolveSceneTarget(emitter->source, targetName, anchorId, normalized)) {
			context.hasTarget = true;
			context.target = {normalized.x * width, normalized.y * height};
		}
	}

	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.update(seconds, context);
}

void emitterRender(void *data, gs_effect_t *effect)
{
	(void)effect;
	EmitterSource *emitter = static_cast<EmitterSource *>(data);

	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->renderer.render(emitter->system);
}

obs_source_info makeEmitterInfo()
{
	obs_source_info info = {};
	info.id = kEmitterSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.icon_type = OBS_ICON_TYPE_CUSTOM;
	info.get_name = emitterGetName;
	info.create = emitterCreate;
	info.destroy = emitterDestroy;
	info.update = emitterUpdate;
	info.get_defaults = emitterDefaults;
	info.get_properties = emitterProperties;
	info.get_width = emitterWidth;
	info.get_height = emitterHeight;
	info.video_tick = emitterTick;
	info.video_render = emitterRender;
	return info;
}

} // namespace

void registerEmitterSource()
{
	static obs_source_info info = makeEmitterInfo();
	obs_register_source(&info);
}

void sourceResetRequested(obs_source_t *source)
{
	EmitterSource *emitter = lookup(source);
	if (!emitter)
		return;

	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.reset();
}

EmitterConfig configOfSource(obs_source_t *source)
{
	if (!source)
		return EmitterConfig{};

	obs_data_t *settings = obs_source_get_settings(source);
	EmitterConfig config = serialize::configFromData(settings);
	obs_data_release(settings);
	return config;
}

void applyConfigToSource(obs_source_t *source, const EmitterConfig &config)
{
	if (!source)
		return;

	// One write and one update: the source reconfigures itself once, rather than once per
	// section the designer touched.
	obs_data_t *settings = obs_source_get_settings(source);
	serialize::configToData(settings, config);
	obs_source_update(source, settings);
	obs_data_release(settings);
}

} // namespace atom
