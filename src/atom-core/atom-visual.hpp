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
#include "atom-modules.hpp"

#include <memory>

namespace atom {

/// Everything a backend needs to draw one atom, with no knowledge of the design model.
struct AtomVisual {
	Vec2 position;
	float size = 0.0f;
	float rotation = 0.0f;
	Color color;

	/// Glow quad drawn behind the atom. Skipped when `bloomAlpha` is zero.
	float bloomSize = 0.0f;
	float bloomAlpha = 0.0f;
	Color bloomColor;

	bool additive = true;
	/// Normalized age after lifetime falloff shaping, for anything else the backend wants to vary.
	float shapedAge = 0.0f;
};

/// Turns atoms plus a design into drawable values.
///
/// Both backends (the OBS graphics renderer and the designer's preview) use this, so a design
/// always looks the same in the preview as it does on stream.
class DesignEvaluator {
public:
	DesignEvaluator();
	~DesignEvaluator();

	DesignEvaluator(const DesignEvaluator &) = delete;
	DesignEvaluator &operator=(const DesignEvaluator &) = delete;

	/// Rebuilds the cached modules. Call whenever the design or physics config changes.
	void rebuild(const AtomDesign &design, const PhysicsConfig &physics);

	AtomVisual evaluate(const Atom &atom) const;

	/// Shaped normalized age, exposed for behaviours and backends that need it separately.
	float shapedAge(const Atom &atom) const;

	const AtomLayer *layerFor(const Atom &atom) const;
	const TrailStyle *trailStyleFor(const Atom &atom) const;

private:
	struct LayerModules {
		std::unique_ptr<FadePath> fade;
		std::unique_ptr<TrailStyle> trail;
	};

	Color evaluateColor(const AtomLayer &layer, const Atom &atom, float t) const;

	AtomDesign design_;
	PhysicsConfig physics_;
	std::vector<LayerModules> modules_;
	std::unique_ptr<LifetimeFalloff> falloff_;
};

/// Rotates a colour's hue by `turns` (1.0 is a full rotation). Alpha is untouched.
Color rotateHue(const Color &color, float turns);

} // namespace atom
