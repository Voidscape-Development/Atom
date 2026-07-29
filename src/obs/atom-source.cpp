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
#include "atom-audio-meter.hpp"
#include "atom-properties.hpp"
#include "atom-renderer.hpp"
#include "atom-serialize.hpp"
#include "atom-core/atom-modulation.hpp"
#include "atom-core/atom-system.hpp"
#include "plugin-support.h"

#include <graphics/matrix4.h>
#include <graphics/vec3.h>

#include <map>
#include <mutex>
#include <vector>

namespace atom {

namespace {

struct EmitterSource {
	obs_source_t *source = nullptr;
	AtomSystem system;
	AtomRenderer renderer;
	ModulationEngine modulation;
	AudioMeter audio;

	/// Configuration as saved. Modulation produces a per-frame variant of this.
	EmitterConfig base;
	EmitterConfig modulated;

	/// Bursts requested by a hotkey, the proc handler or obs-websocket since the last tick.
	int pendingBursts = 0;
	float time = 0.0f;

	obs_hotkey_id burstHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id resetHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id toggleHotkey = OBS_INVALID_HOTKEY_ID;

	/// Guards everything above between the UI, audio and video threads.
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

/// Finds an emitter by source name. Used by the trigger commands, which address sources the way a
/// hotkey or a websocket client would.
EmitterSource *lookupByName(const std::string &name)
{
	std::lock_guard<std::mutex> lock(g_instancesMutex);
	for (const auto &entry : g_instances) {
		const char *sourceName = obs_source_get_name(entry.first);
		if (sourceName && name == sourceName)
			return entry.second;
	}
	return nullptr;
}

std::vector<std::string> splitNames(const std::string &list)
{
	std::vector<std::string> names;
	size_t start = 0;
	while (start <= list.size()) {
		const size_t end = list.find(',', start);
		std::string name = list.substr(start, end == std::string::npos ? std::string::npos : end - start);

		const size_t first = name.find_first_not_of(" \t");
		const size_t last = name.find_last_not_of(" \t");
		if (first != std::string::npos)
			names.push_back(name.substr(first, last - first + 1));

		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return names;
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

/// Maps a point on another item's unit square into the emitter's own normalized space.
Vec2 mapIntoEmitter(const matrix4 &targetBox, const matrix4 &emitterInverse, const Vec2 &unitPoint)
{
	vec3 point;
	vec3_set(&point, unitPoint.x, unitPoint.y, 0.0f);
	vec3_transform(&point, &point, &targetBox);
	vec3_transform(&point, &point, &emitterInverse);
	return {point.x, point.y};
}

struct SceneWalk {
	obs_source_t *self = nullptr;
	const char *selfName = nullptr;

	/// Endpoint tracking.
	const char *targetName = nullptr;
	std::string anchorId;
	Vec2 target;
	bool foundTarget = false;

	/// Collision and attraction tracking.
	bool wantObjects = false;
	bool trackAll = false;
	float padding = 0.0f;
	std::vector<std::string> names;
	std::vector<SceneObject> objects;

	float width = 1.0f;
	float height = 1.0f;
};

/// Builds an emitter-local rectangle from a scene item's box transform.
SceneObject objectFromItem(obs_sceneitem_t *item, const matrix4 &emitterInverse, const SceneWalk &walk)
{
	matrix4 box;
	obs_sceneitem_get_box_transform(item, &box);

	const Vec2 corners[4] = {
		mapIntoEmitter(box, emitterInverse, {0.0f, 0.0f}),
		mapIntoEmitter(box, emitterInverse, {1.0f, 0.0f}),
		mapIntoEmitter(box, emitterInverse, {1.0f, 1.0f}),
		mapIntoEmitter(box, emitterInverse, {0.0f, 1.0f}),
	};

	const Vec2 scale{walk.width, walk.height};
	const Vec2 topLeft{corners[0].x * scale.x, corners[0].y * scale.y};
	const Vec2 topRight{corners[1].x * scale.x, corners[1].y * scale.y};
	const Vec2 bottomLeft{corners[3].x * scale.x, corners[3].y * scale.y};

	SceneObject object;
	object.center =
		(topLeft + topRight + Vec2{corners[2].x * scale.x, corners[2].y * scale.y} + bottomLeft) * 0.25f;
	object.halfSize = {std::max(1.0f, (topRight - topLeft).length() * 0.5f + walk.padding),
			   std::max(1.0f, (bottomLeft - topLeft).length() * 0.5f + walk.padding)};

	const Vec2 edge = topRight - topLeft;
	object.rotation = rad2deg(std::atan2(edge.y, edge.x));
	return object;
}

bool walkScene(obs_scene_t *scene, SceneWalk &walk)
{
	obs_sceneitem_t *selfItem = obs_scene_find_source_recursive(scene, walk.selfName);
	if (!selfItem)
		return false;

	matrix4 emitterBox;
	matrix4 emitterInverse;
	obs_sceneitem_get_box_transform(selfItem, &emitterBox);
	if (!matrix4_inv(&emitterInverse, &emitterBox))
		return false;

	if (walk.targetName && *walk.targetName) {
		if (obs_sceneitem_t *targetItem = obs_scene_find_source_recursive(scene, walk.targetName)) {
			matrix4 targetBox;
			obs_sceneitem_get_box_transform(targetItem, &targetBox);

			static Random random(0x51F3u);
			walk.target = mapIntoEmitter(targetBox, emitterInverse, anchorPoint(walk.anchorId, random));
			walk.foundTarget = true;
		}
	}

	if (walk.wantObjects) {
		struct ItemContext {
			SceneWalk *walk;
			const matrix4 *emitterInverse;
		} context{&walk, &emitterInverse};

		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				ItemContext *ctx = static_cast<ItemContext *>(param);
				obs_source_t *itemSource = obs_sceneitem_get_source(item);
				if (!itemSource || itemSource == ctx->walk->self)
					return true;
				if (!obs_sceneitem_visible(item))
					return true;

				const char *name = obs_source_get_name(itemSource);
				if (!name)
					return true;

				if (!ctx->walk->trackAll) {
					bool wanted = false;
					for (const std::string &candidate : ctx->walk->names) {
						if (candidate == name) {
							wanted = true;
							break;
						}
					}
					if (!wanted)
						return true;
				}

				ctx->walk->objects.push_back(objectFromItem(item, *ctx->emitterInverse, *ctx->walk));
				return true;
			},
			&context);
	}

	return true;
}

/// Resolves everything the emitter needs from the scene it lives in, in one walk.
void resolveScene(SceneWalk &walk)
{
	if (!walk.selfName)
		return;

	obs_enum_scenes(
		[](void *param, obs_source_t *sceneSource) {
			SceneWalk *ctx = static_cast<SceneWalk *>(param);
			obs_scene_t *scene = obs_scene_from_source(sceneSource);
			if (!scene)
				return true;
			// Stop at the first scene that contains the emitter.
			return !walkScene(scene, *ctx);
		},
		&walk);
}

// ---------------------------------------------------------------------------------------------
// Triggers
// ---------------------------------------------------------------------------------------------

void queueBurst(EmitterSource *emitter, int count)
{
	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->pendingBursts += count > 0 ? count : std::max(1, emitter->base.emission.burstCount);
}

void onBurstHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	(void)id;
	(void)hotkey;
	if (pressed)
		queueBurst(static_cast<EmitterSource *>(data), 0);
}

void onResetHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	(void)id;
	(void)hotkey;
	if (!pressed)
		return;

	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.reset();
}

void onToggleHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed)
{
	(void)id;
	(void)hotkey;
	if (!pressed)
		return;

	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.setEmitting(!emitter->system.emitting());
}

void procBurst(void *data, calldata_t *cd)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	long long count = 0;
	calldata_get_int(cd, "count", &count);
	queueBurst(emitter, static_cast<int>(count));
}

void procReset(void *data, calldata_t *cd)
{
	(void)cd;
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.reset();
}

void procSetEmitting(void *data, calldata_t *cd)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	bool enabled = true;
	calldata_get_bool(cd, "enabled", &enabled);

	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.setEmitting(enabled);
}

void procGetStatus(void *data, calldata_t *cd)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	std::lock_guard<std::mutex> lock(emitter->mutex);
	calldata_set_int(cd, "atoms", static_cast<long long>(emitter->system.count()));
	calldata_set_bool(cd, "emitting", emitter->system.emitting());
}

void registerTriggers(EmitterSource *emitter, obs_source_t *source)
{
	emitter->burstHotkey = obs_hotkey_register_source(source, "atom.burst", obs_module_text("Atom.Hotkey.Burst"),
							  onBurstHotkey, emitter);
	emitter->resetHotkey = obs_hotkey_register_source(source, "atom.reset", obs_module_text("Atom.Hotkey.Reset"),
							  onResetHotkey, emitter);
	emitter->toggleHotkey = obs_hotkey_register_source(source, "atom.toggle", obs_module_text("Atom.Hotkey.Toggle"),
							   onToggleHotkey, emitter);

	// Procedures let scripts and other plugins drive the emitter; obs-websocket clients go
	// through the vendor requests in atom-triggers.cpp, which call the same code.
	proc_handler_t *handler = obs_source_get_proc_handler(source);
	proc_handler_add(handler, "void burst(in int count)", procBurst, emitter);
	proc_handler_add(handler, "void reset()", procReset, emitter);
	proc_handler_add(handler, "void set_emitting(in bool enabled)", procSetEmitting, emitter);
	proc_handler_add(handler, "void get_status(out int atoms, out bool emitting)", procGetStatus, emitter);
}

void unregisterTriggers(EmitterSource *emitter)
{
	for (obs_hotkey_id id : {emitter->burstHotkey, emitter->resetHotkey, emitter->toggleHotkey}) {
		if (id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(id);
	}
	emitter->burstHotkey = OBS_INVALID_HOTKEY_ID;
	emitter->resetHotkey = OBS_INVALID_HOTKEY_ID;
	emitter->toggleHotkey = OBS_INVALID_HOTKEY_ID;
}

// ---------------------------------------------------------------------------------------------
// Source callbacks
// ---------------------------------------------------------------------------------------------

const char *emitterGetName(void *)
{
	return obs_module_text("Atom.Emitter");
}

void applyConfiguration(EmitterSource *emitter, const EmitterConfig &config)
{
	emitter->base = config;
	emitter->modulation.setRoutes(config.modulation);
	emitter->audio.setEnvelope(config.audio.attack, config.audio.release, config.audio.gain);
	emitter->audio.attach(config.audio.sourceName);
	emitter->system.configure(config);
}

void *emitterCreate(obs_data_t *settings, obs_source_t *source)
{
	registerBuiltinModules();

	EmitterSource *emitter = new EmitterSource();
	emitter->source = source;
	applyConfiguration(emitter, serialize::configFromData(settings));
	registerTriggers(emitter, source);

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

	unregisterTriggers(emitter);
	emitter->audio.detach();

	obs_enter_graphics();
	emitter->renderer.releaseBuffers();
	obs_leave_graphics();

	delete emitter;
}

void emitterUpdate(void *data, obs_data_t *settings)
{
	EmitterSource *emitter = static_cast<EmitterSource *>(data);
	const EmitterConfig config = serialize::configFromData(settings);

	std::lock_guard<std::mutex> lock(emitter->mutex);
	applyConfiguration(emitter, config);
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

	// Walking OBS' scene list touches OBS locks, so it happens between our own locks rather
	// than under them.
	SceneWalk walk;
	std::string targetName;
	{
		std::lock_guard<std::mutex> lock(emitter->mutex);
		const EmitterConfig &config = emitter->base;

		walk.self = emitter->source;
		walk.selfName = obs_source_get_name(emitter->source);
		walk.width = static_cast<float>(config.emission.width);
		walk.height = static_cast<float>(config.emission.height);

		if (emitter->system.needsHostTarget()) {
			targetName = config.physics.endpoint.sourceName;
			walk.anchorId = config.physics.endpoint.anchorId;
		}

		walk.trackAll = config.scene.trackAll;
		walk.padding = config.scene.padding;
		walk.names = splitNames(config.scene.sources);
		walk.wantObjects = walk.trackAll || !walk.names.empty();
	}

	walk.targetName = targetName.empty() ? nullptr : targetName.c_str();
	if (walk.targetName || walk.wantObjects)
		resolveScene(walk);

	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->time += seconds;

	// Modulation runs on the saved configuration, so routes always modulate the same baseline
	// instead of compounding frame after frame.
	ModContext modContext;
	modContext.time = emitter->time;
	modContext.dt = seconds;
	modContext.audio = emitter->audio.levels();

	if (emitter->modulation.apply(emitter->base, modContext, emitter->modulated))
		emitter->system.setModulatedConfig(emitter->modulated);

	SimContext context;
	if (walk.foundTarget) {
		context.hasTarget = true;
		context.target = {walk.target.x * emitter->system.width(), walk.target.y * emitter->system.height()};
	}
	context.objects = std::move(walk.objects);

	if (emitter->pendingBursts > 0) {
		emitter->system.burst(emitter->pendingBursts);
		emitter->pendingBursts = 0;
	}

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

bool sourceBurst(const std::string &sourceName, int count)
{
	EmitterSource *emitter = lookupByName(sourceName);
	if (!emitter)
		return false;

	queueBurst(emitter, count);
	return true;
}

bool sourceReset(const std::string &sourceName)
{
	EmitterSource *emitter = lookupByName(sourceName);
	if (!emitter)
		return false;

	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.reset();
	return true;
}

bool sourceSetEmitting(const std::string &sourceName, bool emitting)
{
	EmitterSource *emitter = lookupByName(sourceName);
	if (!emitter)
		return false;

	std::lock_guard<std::mutex> lock(emitter->mutex);
	emitter->system.setEmitting(emitting);
	return true;
}

bool sourceStatus(const std::string &sourceName, int &atoms, bool &emitting)
{
	EmitterSource *emitter = lookupByName(sourceName);
	if (!emitter)
		return false;

	std::lock_guard<std::mutex> lock(emitter->mutex);
	atoms = static_cast<int>(emitter->system.count());
	emitting = emitter->system.emitting();
	return true;
}

} // namespace atom
