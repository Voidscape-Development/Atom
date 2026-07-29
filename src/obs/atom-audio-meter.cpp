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

#include "atom-audio-meter.hpp"
#include "plugin-support.h"

#include <obs-module.h>

#include <cmath>

namespace atom {

namespace {

/// One-pole coefficient for a given corner frequency.
float onePole(float frequency, float sampleRate)
{
	if (sampleRate <= 0.0f)
		return 1.0f;
	return 1.0f - std::exp(-2.0f * kPi * frequency / sampleRate);
}

/// Finds a source by name and returns a strong reference, or nullptr.
obs_source_t *findSource(const std::string &name)
{
	return name.empty() ? nullptr : obs_get_source_by_name(name.c_str());
}

} // namespace

AudioMeter::~AudioMeter()
{
	detach();
}

void AudioMeter::attach(const std::string &sourceName)
{
	if (sourceName == sourceName_)
		return;

	detach();
	sourceName_ = sourceName;
	if (sourceName_.empty())
		return;

	obs_source_t *source = findSource(sourceName_);
	if (!source) {
		// The source may simply not exist yet; the next settings change tries again.
		return;
	}

	obs_source_add_audio_capture_callback(source, captureCallback, this);
	weakSource_ = obs_source_get_weak_source(source);
	obs_source_release(source);
	valid_.store(true);
}

void AudioMeter::detach()
{
	if (weakSource_) {
		if (obs_source_t *source = obs_weak_source_get_source(weakSource_)) {
			obs_source_remove_audio_capture_callback(source, captureCallback, this);
			obs_source_release(source);
		}
		obs_weak_source_release(weakSource_);
		weakSource_ = nullptr;
	}

	sourceName_.clear();
	valid_.store(false);
	level_.store(0.0f);
	peak_.store(0.0f);
	low_.store(0.0f);
	mid_.store(0.0f);
	high_.store(0.0f);
}

void AudioMeter::setEnvelope(float attack, float release, float gain)
{
	attack_.store(std::max(0.0f, attack));
	release_.store(std::max(0.0f, release));
	gain_.store(std::max(0.0f, gain));
}

AudioLevels AudioMeter::levels() const
{
	AudioLevels out;
	out.valid = valid_.load();
	if (!out.valid)
		return out;

	const float gain = gain_.load();
	out.level = saturate(level_.load() * gain);
	out.peak = saturate(peak_.load() * gain);
	out.low = saturate(low_.load() * gain);
	out.mid = saturate(mid_.load() * gain);
	out.high = saturate(high_.load() * gain);
	return out;
}

void AudioMeter::captureCallback(void *param, obs_source_t *source, const struct audio_data *data, bool muted)
{
	(void)source;
	static_cast<AudioMeter *>(param)->process(data, muted);
}

void AudioMeter::process(const struct audio_data *data, bool muted)
{
	if (!data || !data->data[0] || data->frames == 0)
		return;

	const float sampleRate = static_cast<float>(audio_output_get_sample_rate(obs_get_audio()));
	const float *samples = reinterpret_cast<const float *>(data->data[0]);
	const size_t frames = data->frames;

	if (muted) {
		level_.store(0.0f);
		peak_.store(0.0f);
		low_.store(0.0f);
		mid_.store(0.0f);
		high_.store(0.0f);
		return;
	}

	// Two cascaded one-pole filters split the signal into low / mid / high without needing a
	// real filter bank: enough to tell a kick from a hi-hat.
	const float lowCoefficient = onePole(180.0f, sampleRate);
	const float midCoefficient = onePole(2000.0f, sampleRate);

	double sum = 0.0;
	double sumLow = 0.0;
	double sumMid = 0.0;
	double sumHigh = 0.0;
	float peak = 0.0f;

	for (size_t i = 0; i < frames; ++i) {
		const float sample = samples[i];
		lowPass_ += (sample - lowPass_) * lowCoefficient;
		midPass_ += (sample - midPass_) * midCoefficient;

		const float low = lowPass_;
		const float mid = midPass_ - lowPass_;
		const float high = sample - midPass_;

		sum += static_cast<double>(sample) * sample;
		sumLow += static_cast<double>(low) * low;
		sumMid += static_cast<double>(mid) * mid;
		sumHigh += static_cast<double>(high) * high;
		peak = std::max(peak, std::abs(sample));
	}

	const float count = static_cast<float>(frames);
	const float rms = std::sqrt(static_cast<float>(sum) / count);
	const float rmsLow = std::sqrt(static_cast<float>(sumLow) / count);
	const float rmsMid = std::sqrt(static_cast<float>(sumMid) / count);
	const float rmsHigh = std::sqrt(static_cast<float>(sumHigh) / count);

	// Envelope follower: fast up, slow down, so a hit reads as a hit.
	const float dt = count / std::max(1.0f, sampleRate);
	const float attack = attack_.load();
	const float release = release_.load();
	const auto follow = [dt, attack, release](float current, float target) {
		const float time = target > current ? attack : release;
		if (time <= 1e-4f)
			return target;
		return lerp(current, target, saturate(1.0f - std::exp(-dt / time)));
	};

	envelope_ = follow(envelope_, rms);
	envelopeLow_ = follow(envelopeLow_, rmsLow);
	envelopeMid_ = follow(envelopeMid_, rmsMid);
	envelopeHigh_ = follow(envelopeHigh_, rmsHigh);

	level_.store(envelope_);
	peak_.store(peak);
	low_.store(envelopeLow_);
	mid_.store(envelopeMid_);
	high_.store(envelopeHigh_);
	valid_.store(true);
}

} // namespace atom
