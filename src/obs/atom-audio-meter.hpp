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

#include "atom-core/atom-modulation.hpp"

#include <obs.h>

#include <atomic>
#include <string>

namespace atom {

/// Measures the level of one OBS source, split into three coarse bands.
///
/// Metering happens on OBS' audio thread and the result is published through atomics; the video
/// thread reads a snapshot each frame and hands it to the modulation engine.
class AudioMeter {
public:
	AudioMeter() = default;
	~AudioMeter();

	AudioMeter(const AudioMeter &) = delete;
	AudioMeter &operator=(const AudioMeter &) = delete;

	/// Listens to the named source, or stops listening when the name is empty. Re-attaching to
	/// the same name is a no-op, so this is safe to call every time settings change.
	void attach(const std::string &sourceName);
	void detach();

	void setEnvelope(float attack, float release, float gain);

	/// Thread-safe snapshot for the current frame.
	AudioLevels levels() const;

	const std::string &sourceName() const { return sourceName_; }

private:
	static void captureCallback(void *param, obs_source_t *source, const struct audio_data *data, bool muted);
	void process(const struct audio_data *data, bool muted);

	obs_weak_source_t *weakSource_ = nullptr;
	std::string sourceName_;

	std::atomic<float> level_{0.0f};
	std::atomic<float> peak_{0.0f};
	std::atomic<float> low_{0.0f};
	std::atomic<float> mid_{0.0f};
	std::atomic<float> high_{0.0f};
	std::atomic<bool> valid_{false};

	std::atomic<float> attack_{0.02f};
	std::atomic<float> release_{0.18f};
	std::atomic<float> gain_{1.0f};

	/// Filter and envelope state, only touched on the audio thread.
	float lowPass_ = 0.0f;
	float midPass_ = 0.0f;
	float envelope_ = 0.0f;
	float envelopeLow_ = 0.0f;
	float envelopeMid_ = 0.0f;
	float envelopeHigh_ = 0.0f;
};

} // namespace atom
