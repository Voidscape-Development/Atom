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

#include "atom-triggers.hpp"
#include "atom-source.hpp"
#include "plugin-support.h"

#include <obs-module.h>

#include <string>

namespace atom {

namespace {

/// Mirrors obs-websocket's vendor request callback signature.
using VendorRequestCallback = void (*)(obs_data_t *requestData, obs_data_t *responseData, void *privateData);

struct VendorRequestCallbackPair {
	VendorRequestCallback callback;
	void *privateData;
};

void *g_vendor = nullptr;

std::string requestedSource(obs_data_t *requestData)
{
	const char *name = obs_data_get_string(requestData, "source");
	return name ? name : "";
}

void finish(obs_data_t *responseData, bool ok, const char *error = nullptr)
{
	obs_data_set_bool(responseData, "ok", ok);
	if (!ok && error)
		obs_data_set_string(responseData, "error", error);
}

void onBurst(obs_data_t *requestData, obs_data_t *responseData, void *)
{
	const int count = static_cast<int>(obs_data_get_int(requestData, "count"));
	finish(responseData, sourceBurst(requestedSource(requestData), count), "no such Atom Emitter");
}

void onReset(obs_data_t *requestData, obs_data_t *responseData, void *)
{
	finish(responseData, sourceReset(requestedSource(requestData)), "no such Atom Emitter");
}

void onSetEmitting(obs_data_t *requestData, obs_data_t *responseData, void *)
{
	obs_data_set_default_bool(requestData, "emitting", true);
	const bool emitting = obs_data_get_bool(requestData, "emitting");
	finish(responseData, sourceSetEmitting(requestedSource(requestData), emitting), "no such Atom Emitter");
}

void onStatus(obs_data_t *requestData, obs_data_t *responseData, void *)
{
	int atoms = 0;
	bool emitting = false;
	const bool ok = sourceStatus(requestedSource(requestData), atoms, emitting);
	if (ok) {
		obs_data_set_int(responseData, "atoms", atoms);
		obs_data_set_bool(responseData, "emitting", emitting);
	}
	finish(responseData, ok, "no such Atom Emitter");
}

/// obs-websocket publishes its vendor API through OBS' global proc handler, so this reaches it
/// without linking against obs-websocket or vendoring its header.
void *registerVendor(const char *name)
{
	proc_handler_t *handler = obs_get_proc_handler();
	if (!handler)
		return nullptr;

	calldata_t data = {};
	calldata_set_string(&data, "name", name);

	void *vendor = nullptr;
	if (proc_handler_call(handler, "obs_websocket_register_vendor", &data))
		vendor = calldata_ptr(&data, "vendor");

	calldata_free(&data);
	return vendor;
}

bool registerRequest(void *vendor, const char *type, VendorRequestCallback callback)
{
	proc_handler_t *handler = obs_get_proc_handler();
	if (!handler || !vendor)
		return false;

	VendorRequestCallbackPair pair{callback, nullptr};

	calldata_t data = {};
	calldata_set_ptr(&data, "vendor", vendor);
	calldata_set_string(&data, "type", type);
	calldata_set_ptr(&data, "callback", &pair);

	const bool ok = proc_handler_call(handler, "obs_websocket_vendor_register_request", &data);
	calldata_free(&data);
	return ok;
}

} // namespace

void registerTriggerApi()
{
	g_vendor = registerVendor("atom");
	if (!g_vendor) {
		obs_log(LOG_INFO, "obs-websocket not available; hotkeys and source procedures still work");
		return;
	}

	const bool ok = registerRequest(g_vendor, "burst", onBurst) & registerRequest(g_vendor, "reset", onReset) &
			registerRequest(g_vendor, "set_emitting", onSetEmitting) &
			registerRequest(g_vendor, "status", onStatus);

	if (ok)
		obs_log(LOG_INFO, "registered obs-websocket vendor 'atom'");
	else
		obs_log(LOG_WARNING, "obs-websocket vendor 'atom' registered with missing requests");
}

} // namespace atom
