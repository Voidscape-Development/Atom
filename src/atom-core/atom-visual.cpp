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

#include "atom-visual.hpp"

namespace atom {

Color rotateHue(const Color &color, float turns)
{
	if (std::abs(turns) < 1e-4f)
		return color;

	// YIQ hue rotation: cheap, and close enough for per-atom colour scatter.
	const float angle = turns * kTwoPi;
	const float c = std::cos(angle);
	const float s = std::sin(angle);

	const float y = 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
	const float i = 0.596f * color.r - 0.274f * color.g - 0.322f * color.b;
	const float q = 0.211f * color.r - 0.523f * color.g + 0.312f * color.b;

	const float ri = i * c - q * s;
	const float rq = i * s + q * c;

	return {saturate(y + 0.956f * ri + 0.621f * rq), saturate(y - 0.272f * ri - 0.647f * rq),
		saturate(y - 1.106f * ri + 1.703f * rq), color.a};
}

DesignEvaluator::DesignEvaluator() = default;
DesignEvaluator::~DesignEvaluator() = default;

void DesignEvaluator::rebuild(const AtomDesign &design, const PhysicsConfig &physics)
{
	design_ = design;
	physics_ = physics;

	falloff_ = FalloffRegistry::instance().create(physics.falloffId);
	if (!falloff_)
		falloff_ = FalloffRegistry::instance().create("linear");
	if (falloff_)
		falloff_->configure(physics.falloffParams);

	modules_.clear();
	modules_.resize(design_.layers.size());
	for (size_t i = 0; i < design_.layers.size(); ++i) {
		const AtomLayer &layer = design_.layers[i];
		LayerModules &modules = modules_[i];

		modules.fade = FadePathRegistry::instance().create(layer.fade.pathId);
		if (modules.fade)
			modules.fade->configure(layer.fade.pathParams);

		if (layer.trail.enabled) {
			modules.trail = TrailRegistry::instance().create(layer.trail.styleId);
			if (modules.trail)
				modules.trail->configure(layer.trail.styleParams);
		}
	}
}

float DesignEvaluator::shapedAge(const Atom &atom) const
{
	const float t = atom.normalizedAge();
	if (physics_.falloffId == "custom")
		return saturate(physics_.falloffCurve.sample(t));
	return falloff_ ? saturate(falloff_->shape(t)) : t;
}

const AtomLayer *DesignEvaluator::layerFor(const Atom &atom) const
{
	return atom.layer < design_.layers.size() ? &design_.layers[atom.layer] : nullptr;
}

const TrailStyle *DesignEvaluator::trailStyleFor(const Atom &atom) const
{
	return atom.layer < modules_.size() ? modules_[atom.layer].trail.get() : nullptr;
}

Color DesignEvaluator::evaluateColor(const AtomLayer &layer, const Atom &atom, float t) const
{
	const ColorConfig &color = layer.color;

	if (color.modeId == "solid")
		return color.color;

	if (color.modeId == "random_stop") {
		const std::vector<GradientStop> &stops = color.gradient.stops;
		if (stops.empty())
			return color.color;
		const size_t index = static_cast<size_t>(atom.randomC * static_cast<float>(stops.size()));
		return stops[std::min(index, stops.size() - 1)].color;
	}

	return color.gradient.sample(t);
}

AtomVisual DesignEvaluator::evaluate(const Atom &atom) const
{
	AtomVisual visual;
	visual.position = atom.pos;
	visual.rotation = atom.rotation;

	const AtomLayer *layer = layerFor(atom);
	if (!layer)
		return visual;

	const float t = shapedAge(atom);
	visual.shapedAge = t;
	visual.additive = layer->blendId == "additive";

	// Size over life.
	const float sizeScale = layer->size.overLife.points.empty() ? 1.0f : layer->size.overLife.sample(t);
	visual.size = std::max(layer->size.minimum, atom.size * sizeScale);

	// Colour over life.
	Color color = evaluateColor(*layer, atom, t);
	color = rotateHue(color, atom.hueShift);
	color.r *= atom.tint.r;
	color.g *= atom.tint.g;
	color.b *= atom.tint.b;

	// Alpha over life: fade path, fade-in ramp, flicker, then per-atom alpha offset.
	const FadePath *fade = atom.layer < modules_.size() ? modules_[atom.layer].fade.get() : nullptr;
	float alpha = layer->fade.pathId == "custom" ? layer->fade.customCurve.sample(t)
						     : (fade ? fade->alpha(t, atom) : 1.0f - t);

	if (layer->fade.fadeIn > 1e-4f && t < layer->fade.fadeIn)
		alpha *= t / layer->fade.fadeIn;

	if (layer->fade.flicker > 1e-4f) {
		const float phase = (atom.age * layer->fade.flickerSpeed + atom.randomA) * kTwoPi;
		const float wave = 0.5f + 0.5f * std::sin(phase);
		alpha *= lerp(1.0f, wave, saturate(layer->fade.flicker));
	}

	color.a = saturate(alpha) * color.a * atom.tint.a;
	visual.color = color;

	// Bloom is drawn as a larger, dimmer quad behind the atom: cheap, and it reads as either a
	// light source or a puff of smoke depending on softness.
	if (layer->bloom.amount > 1e-3f) {
		visual.bloomSize = visual.size * std::max(1.0f, layer->bloom.radius);
		visual.bloomAlpha = saturate(color.a * layer->bloom.amount * lerp(1.0f, 0.55f, layer->bloom.softness));
		visual.bloomColor = layer->bloom.inheritColor ? color : layer->bloom.tint;
		visual.bloomColor.a = visual.bloomAlpha;
	}

	return visual;
}

} // namespace atom
