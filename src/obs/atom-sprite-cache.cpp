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

#include "atom-sprite-cache.hpp"
#include "plugin-support.h"

#include <obs-module.h>

namespace atom {

namespace {

/// Sprite masks are rasterized at this resolution; atoms are almost always drawn smaller, so the
/// GPU's linear filtering does the rest.
constexpr uint32_t kSpriteResolution = 128;
/// Samples per axis inside each texel. Cheap anti-aliasing for hard-edged shapes.
constexpr int kSupersample = 2;

std::string valueKey(const Value &value)
{
	if (const bool *b = std::get_if<bool>(&value))
		return *b ? "1" : "0";
	if (const int64_t *i = std::get_if<int64_t>(&value))
		return std::to_string(*i);
	if (const double *d = std::get_if<double>(&value))
		return std::to_string(*d);
	if (const std::string *s = std::get_if<std::string>(&value))
		return *s;
	if (const Color *c = std::get_if<Color>(&value))
		return std::to_string(c->toRGBA());
	if (const Vec2 *v = std::get_if<Vec2>(&value))
		return std::to_string(v->x) + "," + std::to_string(v->y);
	return "?";
}

} // namespace

std::string spriteCacheKey(const std::string &spriteId, const ParamBag &params, const std::string &imagePath)
{
	std::string key = spriteId;
	for (const auto &entry : params.values()) {
		key += '|';
		key += entry.first;
		key += '=';
		key += valueKey(entry.second);
	}
	if (!imagePath.empty()) {
		key += "|@";
		key += imagePath;
	}
	return key;
}

std::vector<uint8_t> SpriteCache::rasterize(const std::string &spriteId, const ParamBag &params, uint32_t size)
{
	std::vector<uint8_t> mask(static_cast<size_t>(size) * size, 0);

	std::unique_ptr<SpriteShape> shape = SpriteRegistry::instance().create(spriteId);
	if (!shape)
		shape = SpriteRegistry::instance().create("soft_circle");
	if (!shape)
		return mask;

	ParamBag configured = params;
	configured.applyDefaults(SpriteRegistry::instance().schemaFor(spriteId));
	shape->configure(configured);

	const float step = 2.0f / static_cast<float>(size);
	const float subStep = step / static_cast<float>(kSupersample);
	const float weight = 1.0f / static_cast<float>(kSupersample * kSupersample);

	for (uint32_t y = 0; y < size; ++y) {
		for (uint32_t x = 0; x < size; ++x) {
			float coverage = 0.0f;
			for (int sy = 0; sy < kSupersample; ++sy) {
				for (int sx = 0; sx < kSupersample; ++sx) {
					const float u = -1.0f + (static_cast<float>(x) + 0.5f) * step +
							(static_cast<float>(sx) - 0.5f) * subStep;
					const float v = -1.0f + (static_cast<float>(y) + 0.5f) * step +
							(static_cast<float>(sy) - 0.5f) * subStep;
					coverage += saturate(shape->sample(u, v)) * weight;
				}
			}
			mask[static_cast<size_t>(y) * size + x] =
				static_cast<uint8_t>(saturate(coverage) * 255.0f + 0.5f);
		}
	}

	return mask;
}

SpriteCache &SpriteCache::instance()
{
	static SpriteCache cache;
	return cache;
}

gs_texture_t *SpriteCache::texture(const std::string &spriteId, const ParamBag &params, const std::string &imagePath)
{
	const std::string key = spriteCacheKey(spriteId, params, imagePath);
	const auto it = textures_.find(key);
	if (it != textures_.end())
		return it->second;

	gs_texture_t *texture = nullptr;

	std::unique_ptr<SpriteShape> shape = SpriteRegistry::instance().create(spriteId);
	if (shape && shape->usesImage()) {
		if (!imagePath.empty()) {
			texture = gs_texture_create_from_file(imagePath.c_str());
			if (!texture)
				obs_log(LOG_WARNING, "could not load atom image '%s'", imagePath.c_str());
		}
	}

	if (!texture) {
		const std::vector<uint8_t> mask = rasterize(spriteId, params, kSpriteResolution);

		// White RGB with the mask in alpha keeps the shader a single multiply against the
		// atom's colour.
		std::vector<uint8_t> rgba(mask.size() * 4);
		for (size_t i = 0; i < mask.size(); ++i) {
			rgba[i * 4 + 0] = 255;
			rgba[i * 4 + 1] = 255;
			rgba[i * 4 + 2] = 255;
			rgba[i * 4 + 3] = mask[i];
		}

		const uint8_t *levels[1] = {rgba.data()};
		texture = gs_texture_create(kSpriteResolution, kSpriteResolution, GS_RGBA, 1, levels, 0);
	}

	textures_[key] = texture;
	return texture;
}

void SpriteCache::clear()
{
	for (auto &entry : textures_) {
		if (entry.second)
			gs_texture_destroy(entry.second);
	}
	textures_.clear();
}

} // namespace atom
