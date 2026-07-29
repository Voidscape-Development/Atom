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

#include "atom-modules.hpp"
#include "atom-modulation.hpp"
#include "atom-noise.hpp"

namespace atom {

namespace {

ParamSpec pFloat(std::string id, std::string label, double def, double min, double max, double step,
		 std::string suffix = {})
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Float;
	spec.defaultValue = def;
	spec.min = min;
	spec.max = max;
	spec.step = step;
	spec.suffix = std::move(suffix);
	return spec;
}

ParamSpec pInt(std::string id, std::string label, int64_t def, double min, double max)
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Int;
	spec.defaultValue = def;
	spec.min = min;
	spec.max = max;
	spec.step = 1.0;
	return spec;
}

ParamSpec pBool(std::string id, std::string label, bool def)
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Bool;
	spec.defaultValue = def;
	return spec;
}

ParamSpec pVec2(std::string id, std::string label, Vec2 def, double min, double max, double step)
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Vec2;
	spec.defaultValue = def;
	spec.min = min;
	spec.max = max;
	spec.step = step;
	return spec;
}

ParamSpec pEnum(std::string id, std::string label, std::string def,
		std::vector<std::pair<std::string, std::string>> items)
{
	ParamSpec spec;
	spec.id = std::move(id);
	spec.label = std::move(label);
	spec.type = ParamType::Enum;
	spec.defaultValue = std::move(def);
	spec.enumItems = std::move(items);
	return spec;
}

ModuleInfo makeInfo(std::string id, std::string label, std::string description, std::string category, int order,
		    ParamSchema params = {})
{
	ModuleInfo info;
	info.id = std::move(id);
	info.label = std::move(label);
	info.description = std::move(description);
	info.category = std::move(category);
	info.order = order;
	info.params = std::move(params);
	return info;
}

// ---------------------------------------------------------------------------------------------
// Emitter shapes
// ---------------------------------------------------------------------------------------------

class PointShape : public EmitterShape {
public:
	void configure(const ParamBag &params) override
	{
		position_ = params.getVec2("position", {0.5f, 0.5f});
		jitter_ = params.getFloat("jitter", 0.0f);
	}

	SpawnSample sample(Random &random, const SimContext &context) const override
	{
		SpawnSample out;
		out.position = {position_.x * context.width, position_.y * context.height};
		if (jitter_ > 0.0f)
			out.position += random.insideUnitCircle() * jitter_;
		out.normal = (out.position - context.center()).normalized();
		return out;
	}

private:
	Vec2 position_{0.5f, 0.5f};
	float jitter_ = 0.0f;
};

class BoxShape : public EmitterShape {
public:
	void configure(const ParamBag &params) override
	{
		origin_ = params.getVec2("origin", {0.0f, 0.0f});
		size_ = params.getVec2("size", {1.0f, 1.0f});
		distribution_ = params.getString("distribution", "uniform");
	}

	SpawnSample sample(Random &random, const SimContext &context) const override
	{
		const float x0 = origin_.x * context.width;
		const float y0 = origin_.y * context.height;
		const float w = size_.x * context.width;
		const float h = size_.y * context.height;

		float u = random.next();
		float v = random.next();
		if (distribution_ == "gaussian") {
			u = saturate(0.5f + random.nextGaussian() * 0.17f);
			v = saturate(0.5f + random.nextGaussian() * 0.17f);
		}

		SpawnSample out;
		out.position = {x0 + u * w, y0 + v * h};
		out.normal = Vec2{u - 0.5f, v - 0.5f}.normalized();
		return out;
	}

private:
	Vec2 origin_{0.0f, 0.0f};
	Vec2 size_{1.0f, 1.0f};
	std::string distribution_ = "uniform";
};

class EdgeShape : public EmitterShape {
public:
	void configure(const ParamBag &params) override
	{
		edges_ = params.getString("edges", "all");
		inset_ = params.getFloat("inset", 0.0f);
		thickness_ = params.getFloat("thickness", 0.0f);
	}

	SpawnSample sample(Random &random, const SimContext &context) const override
	{
		const float left = inset_;
		const float top = inset_;
		const float right = context.width - inset_;
		const float bottom = context.height - inset_;
		const float w = std::max(1.0f, right - left);
		const float h = std::max(1.0f, bottom - top);

		int edge = 0;
		if (edges_ == "top")
			edge = 0;
		else if (edges_ == "right")
			edge = 1;
		else if (edges_ == "bottom")
			edge = 2;
		else if (edges_ == "left")
			edge = 3;
		else if (edges_ == "horizontal")
			edge = random.chance(0.5f) ? 0 : 2;
		else if (edges_ == "vertical")
			edge = random.chance(0.5f) ? 1 : 3;
		else
			edge = pickWeightedEdge(random, w, h);

		const float t = random.next();
		const float depth = thickness_ > 0.0f ? random.next() * thickness_ : 0.0f;

		SpawnSample out;
		switch (edge) {
		case 0:
			out.position = {left + t * w, top + depth};
			out.normal = {0.0f, -1.0f};
			break;
		case 1:
			out.position = {right - depth, top + t * h};
			out.normal = {1.0f, 0.0f};
			break;
		case 2:
			out.position = {left + t * w, bottom - depth};
			out.normal = {0.0f, 1.0f};
			break;
		default:
			out.position = {left + depth, top + t * h};
			out.normal = {-1.0f, 0.0f};
			break;
		}
		return out;
	}

private:
	/// Distributes evenly along the perimeter rather than evenly per edge, so wide sources do not
	/// pile atoms up on their short sides.
	static int pickWeightedEdge(Random &random, float w, float h)
	{
		const float perimeter = 2.0f * (w + h);
		float pick = random.next() * perimeter;
		if (pick < w)
			return 0;
		pick -= w;
		if (pick < h)
			return 1;
		pick -= h;
		if (pick < w)
			return 2;
		return 3;
	}

	std::string edges_ = "all";
	float inset_ = 0.0f;
	float thickness_ = 0.0f;
};

class CenterShape : public EmitterShape {
public:
	void configure(const ParamBag &params) override
	{
		radius_ = params.getFloat("radius", 0.0f);
		hollow_ = params.getBool("hollow", false);
	}

	SpawnSample sample(Random &random, const SimContext &context) const override
	{
		SpawnSample out;
		out.position = context.center();
		Vec2 offset;
		if (radius_ > 0.0f)
			offset = (hollow_ ? random.onUnitCircle() : random.insideUnitCircle()) * radius_;
		out.position += offset;
		out.normal = offset.lengthSquared() > 0.0f ? offset.normalized() : random.onUnitCircle();
		return out;
	}

private:
	float radius_ = 0.0f;
	bool hollow_ = false;
};

class CircleShape : public EmitterShape {
public:
	void configure(const ParamBag &params) override
	{
		center_ = params.getVec2("center", {0.5f, 0.5f});
		radius_ = params.getFloat("radius", 0.4f);
		innerRadius_ = params.getFloat("inner_radius", 0.0f);
		arcStart_ = params.getFloat("arc_start", 0.0f);
		arcLength_ = params.getFloat("arc_length", 360.0f);
	}

	SpawnSample sample(Random &random, const SimContext &context) const override
	{
		const float reference = std::min(context.width, context.height) * 0.5f;
		const float outer = radius_ * reference;
		const float inner = clampf(innerRadius_, 0.0f, radius_) * reference;

		const float angle = deg2rad(arcStart_ + random.next() * arcLength_);
		// Square-root keeps a filled disc uniformly dense.
		const float r = std::sqrt(lerp(inner * inner, outer * outer, random.next()));

		SpawnSample out;
		out.normal = {std::cos(angle), std::sin(angle)};
		out.position = Vec2{center_.x * context.width, center_.y * context.height} + out.normal * r;
		return out;
	}

private:
	Vec2 center_{0.5f, 0.5f};
	float radius_ = 0.4f;
	float innerRadius_ = 0.0f;
	float arcStart_ = 0.0f;
	float arcLength_ = 360.0f;
};

class LineShape : public EmitterShape {
public:
	void configure(const ParamBag &params) override
	{
		from_ = params.getVec2("from", {0.0f, 0.5f});
		to_ = params.getVec2("to", {1.0f, 0.5f});
		spread_ = params.getFloat("spread", 0.0f);
	}

	SpawnSample sample(Random &random, const SimContext &context) const override
	{
		const Vec2 a{from_.x * context.width, from_.y * context.height};
		const Vec2 b{to_.x * context.width, to_.y * context.height};
		const Vec2 along = (b - a).normalized();

		SpawnSample out;
		out.position = lerp(a, b, random.next());
		out.normal = {along.y, -along.x};
		if (spread_ > 0.0f)
			out.position += out.normal * (random.nextSigned() * spread_);
		return out;
	}

private:
	Vec2 from_{0.0f, 0.5f};
	Vec2 to_{1.0f, 0.5f};
	float spread_ = 0.0f;
};

class PolygonShape : public EmitterShape {
public:
	void configure(const ParamBag &params) override
	{
		center_ = params.getVec2("center", {0.5f, 0.5f});
		radius_ = params.getFloat("radius", 0.4f);
		sides_ = std::max<int>(3, static_cast<int>(params.getInt("sides", 5)));
		rotation_ = params.getFloat("rotation", 0.0f);
		filled_ = params.getBool("filled", false);
		starFactor_ = params.getFloat("star_factor", 1.0f);
	}

	SpawnSample sample(Random &random, const SimContext &context) const override
	{
		const float reference = std::min(context.width, context.height) * 0.5f;
		const float outer = radius_ * reference;

		const int corner = random.rangeInt(0, sides_ - 1);
		const float step = kTwoPi / static_cast<float>(sides_);
		const float baseAngle = deg2rad(rotation_) - kPi * 0.5f;

		const Vec2 p0 = cornerPoint(baseAngle + step * corner, outer, corner);
		const Vec2 p1 = cornerPoint(baseAngle + step * (corner + 1), outer, corner + 1);

		Vec2 local = lerp(p0, p1, random.next());
		if (filled_)
			local *= std::sqrt(random.next());

		SpawnSample out;
		out.position = Vec2{center_.x * context.width, center_.y * context.height} + local;
		const Vec2 edge = (p1 - p0).normalized();
		out.normal = {edge.y, -edge.x};
		return out;
	}

private:
	/// `starFactor` under 1 pulls every other corner inwards, which turns the polygon into a star.
	Vec2 cornerPoint(float angle, float radius, int index) const
	{
		const float scale = (index % 2 == 0) ? 1.0f : starFactor_;
		return {std::cos(angle) * radius * scale, std::sin(angle) * radius * scale};
	}

	Vec2 center_{0.5f, 0.5f};
	float radius_ = 0.4f;
	int sides_ = 5;
	float rotation_ = 0.0f;
	bool filled_ = false;
	float starFactor_ = 1.0f;
};

// ---------------------------------------------------------------------------------------------
// Behaviours
// ---------------------------------------------------------------------------------------------

class WindBehavior : public Behavior {
public:
	void configure(const ParamBag &params) override
	{
		strength_ = params.getFloat("strength", 60.0f);
		angle_ = params.getFloat("angle", 0.0f);
		gust_ = params.getFloat("gust", 0.0f);
		gustSpeed_ = params.getFloat("gust_speed", 0.4f);
	}

	void apply(Atom &atom, float dt, const SimContext &context) override
	{
		float strength = strength_;
		if (gust_ > 0.0f) {
			const float wave = noise::value3(atom.pos.x * 0.002f, 0.0f, context.time * gustSpeed_);
			strength *= lerp(1.0f - gust_, 1.0f + gust_, wave);
		}
		atom.vel += angleToVector(angle_) * (strength * dt);
	}

private:
	float strength_ = 60.0f;
	float angle_ = 0.0f;
	float gust_ = 0.0f;
	float gustSpeed_ = 0.4f;
};

class VortexBehavior : public Behavior {
public:
	void configure(const ParamBag &params) override
	{
		center_ = params.getVec2("center", {0.5f, 0.5f});
		strength_ = params.getFloat("strength", 180.0f);
		inward_ = params.getFloat("inward", 0.0f);
		falloff_ = params.getFloat("falloff", 300.0f);
	}

	void apply(Atom &atom, float dt, const SimContext &context) override
	{
		const Vec2 center{center_.x * context.width, center_.y * context.height};
		const Vec2 delta = atom.pos - center;
		const float distance = delta.length();
		if (distance < 1e-3f)
			return;

		const float attenuation = falloff_ > 0.0f ? 1.0f / (1.0f + distance / falloff_) : 1.0f;
		const Vec2 radial = delta * (1.0f / distance);
		const Vec2 tangent{-radial.y, radial.x};

		atom.vel += tangent * (strength_ * attenuation * dt);
		atom.vel += radial * (-inward_ * attenuation * dt);
	}

private:
	Vec2 center_{0.5f, 0.5f};
	float strength_ = 180.0f;
	float inward_ = 0.0f;
	float falloff_ = 300.0f;
};

class WanderBehavior : public Behavior {
public:
	void configure(const ParamBag &params) override
	{
		strength_ = params.getFloat("strength", 90.0f);
		frequency_ = params.getFloat("frequency", 1.2f);
	}

	void apply(Atom &atom, float dt, const SimContext &context) override
	{
		const float phase = context.time * frequency_ + atom.randomA * kTwoPi;
		const Vec2 direction{std::cos(phase * 1.7f + atom.randomB * 6.0f), std::sin(phase)};
		atom.vel += direction * (strength_ * dt);
	}

private:
	float strength_ = 90.0f;
	float frequency_ = 1.2f;
};

class BoundsBehavior : public Behavior {
public:
	void configure(const ParamBag &params) override
	{
		modeId_ = params.getString("mode", "bounce");
		bounce_ = params.getFloat("bounce", 0.5f);
		margin_ = params.getFloat("margin", 0.0f);
	}

	void apply(Atom &atom, float dt, const SimContext &context) override
	{
		(void)dt;
		const float left = -margin_;
		const float top = -margin_;
		const float right = context.width + margin_;
		const float bottom = context.height + margin_;

		if (modeId_ == "kill") {
			if (atom.pos.x < left || atom.pos.x > right || atom.pos.y < top || atom.pos.y > bottom)
				atom.expired = true;
			return;
		}

		if (modeId_ == "wrap") {
			const float w = right - left;
			const float h = bottom - top;
			if (atom.pos.x < left)
				atom.pos.x += w;
			else if (atom.pos.x > right)
				atom.pos.x -= w;
			if (atom.pos.y < top)
				atom.pos.y += h;
			else if (atom.pos.y > bottom)
				atom.pos.y -= h;
			return;
		}

		if (atom.pos.x < left) {
			atom.pos.x = left;
			atom.vel.x = std::abs(atom.vel.x) * bounce_;
		} else if (atom.pos.x > right) {
			atom.pos.x = right;
			atom.vel.x = -std::abs(atom.vel.x) * bounce_;
		}
		if (atom.pos.y < top) {
			atom.pos.y = top;
			atom.vel.y = std::abs(atom.vel.y) * bounce_;
		} else if (atom.pos.y > bottom) {
			atom.pos.y = bottom;
			atom.vel.y = -std::abs(atom.vel.y) * bounce_;
		}
	}

private:
	std::string modeId_ = "bounce";
	float bounce_ = 0.5f;
	float margin_ = 0.0f;
};

/// Bounces, kills or sticks atoms against the scene objects the host tracks.
class SourceCollisionBehavior : public Behavior {
public:
	void configure(const ParamBag &params) override
	{
		modeId_ = params.getString("mode", "bounce");
		bounce_ = params.getFloat("bounce", 0.45f);
		friction_ = params.getFloat("friction", 0.25f);
		margin_ = params.getFloat("margin", 0.0f);
	}

	void apply(Atom &atom, float dt, const SimContext &context) override
	{
		(void)dt;
		if (context.objects.empty())
			return;

		for (const SceneObject &object : context.objects) {
			Vec2 normal;
			const float distance = object.distance(atom.pos, normal) - margin_;
			if (distance > 0.0f)
				continue;

			if (modeId_ == "kill") {
				atom.expired = true;
				return;
			}

			// Push back out along the surface normal, then resolve the velocity.
			atom.pos += normal * (-distance + 0.01f);

			if (modeId_ == "stick") {
				atom.vel = Vec2{0.0f, 0.0f};
				continue;
			}

			const float into = atom.vel.x * normal.x + atom.vel.y * normal.y;
			if (into >= 0.0f)
				continue;

			const Vec2 normalPart = normal * into;
			const Vec2 tangentPart = atom.vel - normalPart;

			if (modeId_ == "slide")
				atom.vel = tangentPart * (1.0f - saturate(friction_));
			else
				atom.vel = tangentPart * (1.0f - saturate(friction_)) - normalPart * saturate(bounce_);
		}
	}

private:
	std::string modeId_ = "bounce";
	float bounce_ = 0.45f;
	float friction_ = 0.25f;
	float margin_ = 0.0f;
};

/// Pulls atoms toward (or pushes them away from) the tracked scene objects.
class SourceAttractBehavior : public Behavior {
public:
	void configure(const ParamBag &params) override
	{
		strength_ = params.getFloat("strength", 300.0f);
		falloff_ = params.getFloat("falloff", 250.0f);
		swirl_ = params.getFloat("swirl", 0.0f);
		range_ = params.getFloat("range", 0.0f);
	}

	void apply(Atom &atom, float dt, const SimContext &context) override
	{
		for (const SceneObject &object : context.objects) {
			const Vec2 delta = object.center - atom.pos;
			const float distance = delta.length();
			if (distance < 1e-3f)
				continue;
			if (range_ > 0.0f && distance > range_)
				continue;

			const Vec2 direction = delta * (1.0f / distance);
			const float attenuation = falloff_ > 0.0f ? 1.0f / (1.0f + distance / falloff_) : 1.0f;

			atom.vel += direction * (strength_ * attenuation * dt);
			if (swirl_ != 0.0f)
				atom.vel += Vec2{-direction.y, direction.x} * (swirl_ * attenuation * dt);
		}
	}

private:
	float strength_ = 300.0f;
	float falloff_ = 250.0f;
	float swirl_ = 0.0f;
	float range_ = 0.0f;
};

// ---------------------------------------------------------------------------------------------
// Modulators
// ---------------------------------------------------------------------------------------------

/// Reads a band of the level the host measured this frame.
class AudioModulator : public Modulator {
public:
	void configure(const ParamBag &params) override
	{
		band_ = params.getString("band", "level");
		gain_ = params.getFloat("gain", 1.0f);
		floor_ = params.getFloat("floor", 0.0f);
		ceiling_ = params.getFloat("ceiling", 1.0f);
		invert_ = params.getBool("invert", false);
	}

	float value(const ModContext &context) override
	{
		if (!context.audio.valid)
			return invert_ ? 1.0f : 0.0f;

		const float raw = context.audio.band(band_) * gain_;
		const float span = std::max(1e-4f, ceiling_ - floor_);
		const float mapped = saturate((raw - floor_) / span);
		return invert_ ? 1.0f - mapped : mapped;
	}

private:
	std::string band_ = "level";
	float gain_ = 1.0f;
	float floor_ = 0.0f;
	float ceiling_ = 1.0f;
	bool invert_ = false;
};

class LfoModulator : public Modulator {
public:
	void configure(const ParamBag &params) override
	{
		rate_ = params.getFloat("rate", 0.5f);
		shape_ = params.getString("shape", "sine");
		phase_ = params.getFloat("phase", 0.0f);
	}

	float value(const ModContext &context) override
	{
		const float t = std::fmod(context.time * rate_ + phase_, 1.0f);
		if (shape_ == "triangle")
			return 1.0f - std::abs(t * 2.0f - 1.0f);
		if (shape_ == "square")
			return t < 0.5f ? 1.0f : 0.0f;
		if (shape_ == "saw")
			return t;
		return 0.5f + 0.5f * std::sin(t * kTwoPi);
	}

private:
	float rate_ = 0.5f;
	std::string shape_ = "sine";
	float phase_ = 0.0f;
};

class NoiseModulator : public Modulator {
public:
	void configure(const ParamBag &params) override
	{
		rate_ = params.getFloat("rate", 1.0f);
		seed_ = static_cast<float>(params.getInt("seed", 1));
	}

	float value(const ModContext &context) override
	{
		return saturate(noise::value3(context.time * rate_, seed_, 0.0f));
	}

private:
	float rate_ = 1.0f;
	float seed_ = 1.0f;
};

class ConstantModulator : public Modulator {
public:
	void configure(const ParamBag &params) override { value_ = params.getFloat("value", 1.0f); }

	float value(const ModContext &context) override
	{
		(void)context;
		return value_;
	}

private:
	float value_ = 1.0f;
};

// ---------------------------------------------------------------------------------------------
// Sprites
// ---------------------------------------------------------------------------------------------

class SoftCircleSprite : public SpriteShape {
public:
	void configure(const ParamBag &params) override { falloff_ = params.getFloat("falloff", 1.6f); }

	float sample(float x, float y) const override
	{
		const float r = std::sqrt(x * x + y * y);
		if (r >= 1.0f)
			return 0.0f;
		return std::pow(1.0f - r, std::max(0.05f, falloff_));
	}

private:
	float falloff_ = 1.6f;
};

class HardCircleSprite : public SpriteShape {
public:
	void configure(const ParamBag &params) override { edge_ = params.getFloat("edge", 0.08f); }

	float sample(float x, float y) const override
	{
		const float r = std::sqrt(x * x + y * y);
		const float edge = std::max(0.001f, edge_);
		return saturate((1.0f - r) / edge);
	}

private:
	float edge_ = 0.08f;
};

class RingSprite : public SpriteShape {
public:
	void configure(const ParamBag &params) override
	{
		thickness_ = params.getFloat("thickness", 0.25f);
		softness_ = params.getFloat("softness", 0.4f);
	}

	float sample(float x, float y) const override
	{
		const float r = std::sqrt(x * x + y * y);
		const float distance = std::abs(1.0f - thickness_ * 0.5f - r);
		const float half = std::max(0.01f, thickness_ * 0.5f);
		const float mask = saturate(1.0f - distance / half);
		return std::pow(mask, lerp(4.0f, 1.0f, saturate(softness_)));
	}

private:
	float thickness_ = 0.25f;
	float softness_ = 0.4f;
};

class SparkSprite : public SpriteShape {
public:
	void configure(const ParamBag &params) override
	{
		points_ = std::max<int>(2, static_cast<int>(params.getInt("points", 4)));
		sharpness_ = params.getFloat("sharpness", 3.0f);
		core_ = params.getFloat("core", 0.18f);
	}

	float sample(float x, float y) const override
	{
		const float r = std::sqrt(x * x + y * y);
		if (r >= 1.0f)
			return 0.0f;

		const float angle = std::atan2(y, x);
		const float lobes = std::abs(std::cos(angle * static_cast<float>(points_) * 0.5f));
		const float spike = std::pow(lobes, std::max(0.1f, sharpness_));
		const float radial = std::pow(1.0f - r, 1.4f);
		const float core = std::pow(saturate(1.0f - r / std::max(0.01f, core_)), 1.5f);
		return saturate(spike * radial + core);
	}

private:
	int points_ = 4;
	float sharpness_ = 3.0f;
	float core_ = 0.18f;
};

class StarSprite : public SpriteShape {
public:
	void configure(const ParamBag &params) override
	{
		points_ = std::max<int>(3, static_cast<int>(params.getInt("points", 5)));
		innerRatio_ = params.getFloat("inner_ratio", 0.45f);
		softness_ = params.getFloat("softness", 0.12f);
	}

	float sample(float x, float y) const override
	{
		const float r = std::sqrt(x * x + y * y);
		if (r >= 1.0f)
			return 0.0f;

		const float angle = std::atan2(y, x);
		const float step = kTwoPi / static_cast<float>(points_);
		const float local = std::fmod(std::fmod(angle, step) + step, step) / step;
		// Triangular wave between the outer point and the inner notch.
		const float wave = std::abs(local * 2.0f - 1.0f);
		const float limit = lerp(innerRatio_, 1.0f, wave);
		return saturate((limit - r) / std::max(0.001f, softness_));
	}

private:
	int points_ = 5;
	float innerRatio_ = 0.45f;
	float softness_ = 0.12f;
};

class SquareSprite : public SpriteShape {
public:
	void configure(const ParamBag &params) override
	{
		rounding_ = params.getFloat("rounding", 0.15f);
		softness_ = params.getFloat("softness", 0.08f);
	}

	float sample(float x, float y) const override
	{
		const float radius = clampf(rounding_, 0.0f, 1.0f);
		const float dx = std::max(std::abs(x) - (1.0f - radius), 0.0f);
		const float dy = std::max(std::abs(y) - (1.0f - radius), 0.0f);
		const float outside = std::sqrt(dx * dx + dy * dy) - radius;
		return saturate(-outside / std::max(0.001f, softness_));
	}

private:
	float rounding_ = 0.15f;
	float softness_ = 0.08f;
};

class SmokeSprite : public SpriteShape {
public:
	void configure(const ParamBag &params) override
	{
		detail_ = params.getFloat("detail", 3.5f);
		contrast_ = params.getFloat("contrast", 1.3f);
		seed_ = static_cast<float>(params.getInt("seed", 3));
	}

	float sample(float x, float y) const override
	{
		const float r = std::sqrt(x * x + y * y);
		if (r >= 1.0f)
			return 0.0f;

		const float scale = std::max(0.5f, detail_);
		float n = noise::value3(x * scale + 8.0f, y * scale + 8.0f, seed_);
		n = 0.6f * n + 0.4f * noise::value3(x * scale * 2.3f, y * scale * 2.3f, seed_ + 5.0f);

		const float body = std::pow(1.0f - r, 1.5f);
		return saturate(std::pow(body * lerp(0.55f, 1.35f, n), std::max(0.2f, contrast_)));
	}

private:
	float detail_ = 3.5f;
	float contrast_ = 1.3f;
	float seed_ = 3.0f;
};

class ImageSprite : public SpriteShape {
public:
	bool usesImage() const override { return true; }

	float sample(float x, float y) const override
	{
		// Used only as the placeholder mask while no image is loaded.
		const float r = std::sqrt(x * x + y * y);
		return r >= 1.0f ? 0.0f : 1.0f - r;
	}
};

// ---------------------------------------------------------------------------------------------
// Trail styles
// ---------------------------------------------------------------------------------------------

class StreakTrail : public TrailStyle {
public:
	void configure(const ParamBag &params) override { taper_ = params.getFloat("taper", 1.0f); }

	TrailSample sample(float t, const Atom &atom) const override
	{
		(void)atom;
		TrailSample out;
		out.widthScale = lerp(1.0f, 1.0f - saturate(taper_), t);
		out.alphaScale = 1.0f - t;
		return out;
	}

private:
	float taper_ = 1.0f;
};

class RibbonTrail : public TrailStyle {
public:
	void configure(const ParamBag &params) override { softness_ = params.getFloat("softness", 0.35f); }

	TrailSample sample(float t, const Atom &atom) const override
	{
		(void)atom;
		TrailSample out;
		out.widthScale = 1.0f;
		out.alphaScale = std::pow(1.0f - t, lerp(0.4f, 2.5f, saturate(softness_)));
		return out;
	}

private:
	float softness_ = 0.35f;
};

class CometTrail : public TrailStyle {
public:
	void configure(const ParamBag &params) override { flare_ = params.getFloat("flare", 0.6f); }

	TrailSample sample(float t, const Atom &atom) const override
	{
		(void)atom;
		TrailSample out;
		out.widthScale = std::pow(1.0f - t, 0.6f) * lerp(1.0f, 1.6f, saturate(flare_));
		out.alphaScale = std::pow(1.0f - t, 2.0f);
		return out;
	}

private:
	float flare_ = 0.6f;
};

class SparkleTrail : public TrailStyle {
public:
	void configure(const ParamBag &params) override
	{
		scatter_ = params.getFloat("scatter", 4.0f);
		density_ = params.getFloat("density", 0.7f);
		twinkle_ = params.getFloat("twinkle", 0.8f);
	}

	bool discrete() const override { return true; }

	TrailSample sample(float t, const Atom &atom) const override
	{
		const float seed = atom.randomA * 91.7f + atom.randomB * 13.3f;
		const float pick = noise::value3(t * 23.0f, seed, 0.0f);

		TrailSample out;
		out.alphaScale = pick < density_ ? (1.0f - t) * lerp(1.0f, pick / std::max(0.01f, density_), twinkle_)
						 : 0.0f;
		out.widthScale = lerp(0.35f, 1.0f, noise::value3(seed, t * 17.0f, 1.0f));
		const float angle = noise::value3(seed + 3.0f, t * 11.0f, 2.0f) * kTwoPi;
		out.offset = Vec2{std::cos(angle), std::sin(angle)} * (scatter_ * t);
		return out;
	}

private:
	float scatter_ = 4.0f;
	float density_ = 0.7f;
	float twinkle_ = 0.8f;
};

// ---------------------------------------------------------------------------------------------
// Fade paths
// ---------------------------------------------------------------------------------------------

class ConstantFade : public FadePath {
public:
	float alpha(float t, const Atom &atom) const override
	{
		(void)t;
		(void)atom;
		return 1.0f;
	}
};

class LinearFade : public FadePath {
public:
	float alpha(float t, const Atom &atom) const override
	{
		(void)atom;
		return 1.0f - t;
	}
};

class EaseOutFade : public FadePath {
public:
	void configure(const ParamBag &params) override { power_ = params.getFloat("power", 2.0f); }

	float alpha(float t, const Atom &atom) const override
	{
		(void)atom;
		return std::pow(1.0f - t, std::max(0.05f, power_));
	}

private:
	float power_ = 2.0f;
};

class HoldFade : public FadePath {
public:
	void configure(const ParamBag &params) override { hold_ = params.getFloat("hold", 0.6f); }

	float alpha(float t, const Atom &atom) const override
	{
		(void)atom;
		const float hold = clampf(hold_, 0.0f, 0.95f);
		if (t <= hold)
			return 1.0f;
		return 1.0f - (t - hold) / (1.0f - hold);
	}

private:
	float hold_ = 0.6f;
};

/// Bright ignition, a dimmer travel phase, then a burnout spike: the firework/flare look.
class FlareFade : public FadePath {
public:
	void configure(const ParamBag &params) override
	{
		ignition_ = params.getFloat("ignition", 0.08f);
		travel_ = params.getFloat("travel", 0.45f);
		burnout_ = params.getFloat("burnout", 0.75f);
	}

	float alpha(float t, const Atom &atom) const override
	{
		(void)atom;
		const float ignition = clampf(ignition_, 0.01f, 0.5f);
		if (t < ignition)
			return t / ignition;

		const float burnout = clampf(burnout_, ignition + 0.05f, 0.99f);
		if (t < burnout) {
			const float local = (t - ignition) / (burnout - ignition);
			return lerp(1.0f, clampf(travel_, 0.0f, 1.0f), smoothstep01(local));
		}

		const float local = (t - burnout) / (1.0f - burnout);
		// Flash back up, then die.
		const float flash = std::sin(local * kPi);
		return saturate(lerp(clampf(travel_, 0.0f, 1.0f), 0.0f, local) + flash * 0.9f);
	}

private:
	float ignition_ = 0.08f;
	float travel_ = 0.45f;
	float burnout_ = 0.75f;
};

class BlinkFade : public FadePath {
public:
	void configure(const ParamBag &params) override
	{
		rate_ = params.getFloat("rate", 6.0f);
		duty_ = params.getFloat("duty", 0.5f);
		decay_ = params.getBool("decay", true);
	}

	float alpha(float t, const Atom &atom) const override
	{
		const float phase = std::fmod(t * std::max(0.1f, rate_) + atom.randomA, 1.0f);
		const float on = phase < clampf(duty_, 0.05f, 0.95f) ? 1.0f : 0.0f;
		return decay_ ? on * (1.0f - t) : on;
	}

private:
	float rate_ = 6.0f;
	float duty_ = 0.5f;
	bool decay_ = true;
};

class PulseFade : public FadePath {
public:
	void configure(const ParamBag &params) override
	{
		cycles_ = params.getFloat("cycles", 2.0f);
		depth_ = params.getFloat("depth", 0.6f);
	}

	float alpha(float t, const Atom &atom) const override
	{
		const float wave = 0.5f + 0.5f * std::sin((t * cycles_ + atom.randomB) * kTwoPi);
		return (1.0f - t) * lerp(1.0f, wave, saturate(depth_));
	}

private:
	float cycles_ = 2.0f;
	float depth_ = 0.6f;
};

/// Placeholder for the "custom" entry: the actual shape comes from FadeConfig::customCurve, which
/// the evaluator samples directly.
class CustomFade : public FadePath {
public:
	float alpha(float t, const Atom &atom) const override
	{
		(void)atom;
		return 1.0f - t;
	}
};

// ---------------------------------------------------------------------------------------------
// Lifetime falloffs
// ---------------------------------------------------------------------------------------------

class LinearFalloff : public LifetimeFalloff {
public:
	float shape(float t) const override { return t; }
};

class EaseInFalloff : public LifetimeFalloff {
public:
	float shape(float t) const override { return t * t; }
};

class EaseOutFalloff : public LifetimeFalloff {
public:
	float shape(float t) const override { return 1.0f - (1.0f - t) * (1.0f - t); }
};

class SmoothFalloff : public LifetimeFalloff {
public:
	float shape(float t) const override { return smoothstep01(t); }
};

class ExponentialFalloff : public LifetimeFalloff {
public:
	void configure(const ParamBag &params) override { rate_ = params.getFloat("rate", 3.0f); }

	float shape(float t) const override
	{
		const float rate = std::max(0.01f, rate_);
		return saturate((1.0f - std::exp(-rate * t)) / (1.0f - std::exp(-rate)));
	}

private:
	float rate_ = 3.0f;
};

class CustomFalloff : public LifetimeFalloff {
public:
	float shape(float t) const override { return t; }
};

// ---------------------------------------------------------------------------------------------
// Endpoint providers
// ---------------------------------------------------------------------------------------------

class NoEndpoint : public EndpointProvider {
public:
	bool resolve(const SimContext &context, const EndpointConfig &config, Vec2 &out) const override
	{
		(void)context;
		(void)config;
		(void)out;
		return false;
	}
};

class PointEndpoint : public EndpointProvider {
public:
	bool resolve(const SimContext &context, const EndpointConfig &config, Vec2 &out) const override
	{
		out = {config.point.x * context.width, config.point.y * context.height};
		return true;
	}
};

class SourceEndpoint : public EndpointProvider {
public:
	bool needsHostTarget() const override { return true; }

	bool resolve(const SimContext &context, const EndpointConfig &config, Vec2 &out) const override
	{
		(void)config;
		if (!context.hasTarget)
			return false;
		out = context.target;
		return true;
	}
};

void registerShapes()
{
	auto &registry = EmitterShapeRegistry::instance();

	registry.add(makeInfo("point", "Atom.Shape.Point", "Atom.Shape.Point.Description", "basic", 10,
			      {pVec2("position", "Atom.Shape.Position", {0.5f, 0.5f}, 0.0, 1.0, 0.001),
			       pFloat("jitter", "Atom.Shape.Jitter", 0.0, 0.0, 400.0, 1.0, "px")}),
		     [] { return std::make_unique<PointShape>(); });

	registry.add(makeInfo("box", "Atom.Shape.Box", "Atom.Shape.Box.Description", "basic", 20,
			      {pVec2("origin", "Atom.Shape.Origin", {0.0f, 0.0f}, -1.0, 2.0, 0.001),
			       pVec2("size", "Atom.Shape.Size", {1.0f, 1.0f}, 0.0, 2.0, 0.001),
			       pEnum("distribution", "Atom.Shape.Distribution", "uniform",
				     {{"uniform", "Atom.Shape.Distribution.Uniform"},
				      {"gaussian", "Atom.Shape.Distribution.Gaussian"}})}),
		     [] { return std::make_unique<BoxShape>(); });

	registry.add(makeInfo("edge", "Atom.Shape.Edge", "Atom.Shape.Edge.Description", "basic", 30,
			      {pEnum("edges", "Atom.Shape.Edges", "all",
				     {{"all", "Atom.Shape.Edges.All"},
				      {"top", "Atom.Shape.Edges.Top"},
				      {"right", "Atom.Shape.Edges.Right"},
				      {"bottom", "Atom.Shape.Edges.Bottom"},
				      {"left", "Atom.Shape.Edges.Left"},
				      {"horizontal", "Atom.Shape.Edges.Horizontal"},
				      {"vertical", "Atom.Shape.Edges.Vertical"}}),
			       pFloat("inset", "Atom.Shape.Inset", 0.0, -500.0, 500.0, 1.0, "px"),
			       pFloat("thickness", "Atom.Shape.Thickness", 0.0, 0.0, 500.0, 1.0, "px")}),
		     [] { return std::make_unique<EdgeShape>(); });

	registry.add(makeInfo("center", "Atom.Shape.Center", "Atom.Shape.Center.Description", "basic", 40,
			      {pFloat("radius", "Atom.Shape.Radius", 0.0, 0.0, 1000.0, 1.0, "px"),
			       pBool("hollow", "Atom.Shape.Hollow", false)}),
		     [] { return std::make_unique<CenterShape>(); });

	registry.add(makeInfo("circle", "Atom.Shape.Circle", "Atom.Shape.Circle.Description", "shape", 50,
			      {pVec2("center", "Atom.Shape.Center", {0.5f, 0.5f}, 0.0, 1.0, 0.001),
			       pFloat("radius", "Atom.Shape.Radius", 0.4, 0.0, 2.0, 0.01),
			       pFloat("inner_radius", "Atom.Shape.InnerRadius", 0.0, 0.0, 2.0, 0.01),
			       pFloat("arc_start", "Atom.Shape.ArcStart", 0.0, -360.0, 360.0, 1.0, "\xC2\xB0"),
			       pFloat("arc_length", "Atom.Shape.ArcLength", 360.0, 0.0, 360.0, 1.0, "\xC2\xB0")}),
		     [] { return std::make_unique<CircleShape>(); });

	registry.add(makeInfo("line", "Atom.Shape.Line", "Atom.Shape.Line.Description", "shape", 60,
			      {pVec2("from", "Atom.Shape.From", {0.0f, 0.5f}, -1.0, 2.0, 0.001),
			       pVec2("to", "Atom.Shape.To", {1.0f, 0.5f}, -1.0, 2.0, 0.001),
			       pFloat("spread", "Atom.Shape.Spread", 0.0, 0.0, 400.0, 1.0, "px")}),
		     [] { return std::make_unique<LineShape>(); });

	registry.add(makeInfo("polygon", "Atom.Shape.Polygon", "Atom.Shape.Polygon.Description", "shape", 70,
			      {pVec2("center", "Atom.Shape.Center", {0.5f, 0.5f}, 0.0, 1.0, 0.001),
			       pFloat("radius", "Atom.Shape.Radius", 0.4, 0.0, 2.0, 0.01),
			       pInt("sides", "Atom.Shape.Sides", 5, 3, 64),
			       pFloat("rotation", "Atom.Shape.Rotation", 0.0, -360.0, 360.0, 1.0, "\xC2\xB0"),
			       pFloat("star_factor", "Atom.Shape.StarFactor", 1.0, 0.05, 1.0, 0.01),
			       pBool("filled", "Atom.Shape.Filled", false)}),
		     [] { return std::make_unique<PolygonShape>(); });
}

void registerBehaviors()
{
	auto &registry = BehaviorRegistry::instance();

	registry.add(makeInfo("wind", "Atom.Behavior.Wind", "Atom.Behavior.Wind.Description", "force", 10,
			      {pFloat("strength", "Atom.Behavior.Strength", 60.0, -2000.0, 2000.0, 1.0, "px/s\xC2\xB2"),
			       pFloat("angle", "Atom.Behavior.Angle", 0.0, -360.0, 360.0, 1.0, "\xC2\xB0"),
			       pFloat("gust", "Atom.Behavior.Gust", 0.0, 0.0, 1.0, 0.01),
			       pFloat("gust_speed", "Atom.Behavior.GustSpeed", 0.4, 0.0, 10.0, 0.05)}),
		     [] { return std::make_unique<WindBehavior>(); });

	registry.add(makeInfo("vortex", "Atom.Behavior.Vortex", "Atom.Behavior.Vortex.Description", "force", 20,
			      {pVec2("center", "Atom.Behavior.Center", {0.5f, 0.5f}, -1.0, 2.0, 0.001),
			       pFloat("strength", "Atom.Behavior.Strength", 180.0, -4000.0, 4000.0, 1.0),
			       pFloat("inward", "Atom.Behavior.Inward", 0.0, -2000.0, 2000.0, 1.0),
			       pFloat("falloff", "Atom.Behavior.Falloff", 300.0, 0.0, 4000.0, 1.0, "px")}),
		     [] { return std::make_unique<VortexBehavior>(); });

	registry.add(makeInfo("wander", "Atom.Behavior.Wander", "Atom.Behavior.Wander.Description", "force", 30,
			      {pFloat("strength", "Atom.Behavior.Strength", 90.0, 0.0, 2000.0, 1.0),
			       pFloat("frequency", "Atom.Behavior.Frequency", 1.2, 0.0, 20.0, 0.05, "Hz")}),
		     [] { return std::make_unique<WanderBehavior>(); });

	registry.add(makeInfo("source_collision", "Atom.Behavior.Collision", "Atom.Behavior.Collision.Description",
			      "rule", 50,
			      {pEnum("mode", "Atom.Behavior.CollisionMode", "bounce",
				     {{"bounce", "Atom.Behavior.CollisionMode.Bounce"},
				      {"slide", "Atom.Behavior.CollisionMode.Slide"},
				      {"stick", "Atom.Behavior.CollisionMode.Stick"},
				      {"kill", "Atom.Behavior.CollisionMode.Kill"}}),
			       pFloat("bounce", "Atom.Behavior.Bounciness", 0.45, 0.0, 1.0, 0.01),
			       pFloat("friction", "Atom.Behavior.Friction", 0.25, 0.0, 1.0, 0.01),
			       pFloat("margin", "Atom.Behavior.Margin", 0.0, -200.0, 200.0, 1.0, "px")}),
		     [] { return std::make_unique<SourceCollisionBehavior>(); });

	registry.add(makeInfo("source_attract", "Atom.Behavior.SourceAttract",
			      "Atom.Behavior.SourceAttract.Description", "force", 60,
			      {pFloat("strength", "Atom.Behavior.Strength", 300.0, -4000.0, 4000.0, 1.0),
			       pFloat("falloff", "Atom.Behavior.Falloff", 250.0, 0.0, 4000.0, 1.0, "px"),
			       pFloat("swirl", "Atom.Behavior.Swirl", 0.0, -4000.0, 4000.0, 1.0),
			       pFloat("range", "Atom.Behavior.Range", 0.0, 0.0, 8000.0, 1.0, "px")}),
		     [] { return std::make_unique<SourceAttractBehavior>(); });

	registry.add(makeInfo("bounds", "Atom.Behavior.Bounds", "Atom.Behavior.Bounds.Description", "rule", 40,
			      {pEnum("mode", "Atom.Behavior.BoundsMode", "bounce",
				     {{"bounce", "Atom.Behavior.BoundsMode.Bounce"},
				      {"wrap", "Atom.Behavior.BoundsMode.Wrap"},
				      {"kill", "Atom.Behavior.BoundsMode.Kill"}}),
			       pFloat("bounce", "Atom.Behavior.Bounciness", 0.5, 0.0, 1.0, 0.01),
			       pFloat("margin", "Atom.Behavior.Margin", 0.0, -1000.0, 1000.0, 1.0, "px")}),
		     [] { return std::make_unique<BoundsBehavior>(); });
}

void registerSprites()
{
	auto &registry = SpriteRegistry::instance();

	registry.add(makeInfo("soft_circle", "Atom.Sprite.SoftCircle", "Atom.Sprite.SoftCircle.Description", "basic",
			      10, {pFloat("falloff", "Atom.Sprite.Falloff", 1.6, 0.1, 8.0, 0.05)}),
		     [] { return std::make_unique<SoftCircleSprite>(); });

	registry.add(makeInfo("hard_circle", "Atom.Sprite.HardCircle", "Atom.Sprite.HardCircle.Description", "basic",
			      20, {pFloat("edge", "Atom.Sprite.Edge", 0.08, 0.005, 1.0, 0.005)}),
		     [] { return std::make_unique<HardCircleSprite>(); });

	registry.add(makeInfo("ring", "Atom.Sprite.Ring", "Atom.Sprite.Ring.Description", "basic", 30,
			      {pFloat("thickness", "Atom.Sprite.Thickness", 0.25, 0.02, 1.0, 0.01),
			       pFloat("softness", "Atom.Sprite.Softness", 0.4, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<RingSprite>(); });

	registry.add(makeInfo("spark", "Atom.Sprite.Spark", "Atom.Sprite.Spark.Description", "effect", 40,
			      {pInt("points", "Atom.Sprite.Points", 4, 2, 12),
			       pFloat("sharpness", "Atom.Sprite.Sharpness", 3.0, 0.5, 12.0, 0.1),
			       pFloat("core", "Atom.Sprite.Core", 0.18, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<SparkSprite>(); });

	registry.add(makeInfo("star", "Atom.Sprite.Star", "Atom.Sprite.Star.Description", "effect", 50,
			      {pInt("points", "Atom.Sprite.Points", 5, 3, 16),
			       pFloat("inner_ratio", "Atom.Sprite.InnerRatio", 0.45, 0.05, 1.0, 0.01),
			       pFloat("softness", "Atom.Sprite.Softness", 0.12, 0.001, 1.0, 0.005)}),
		     [] { return std::make_unique<StarSprite>(); });

	registry.add(makeInfo("square", "Atom.Sprite.Square", "Atom.Sprite.Square.Description", "basic", 60,
			      {pFloat("rounding", "Atom.Sprite.Rounding", 0.15, 0.0, 1.0, 0.01),
			       pFloat("softness", "Atom.Sprite.Softness", 0.08, 0.001, 1.0, 0.005)}),
		     [] { return std::make_unique<SquareSprite>(); });

	registry.add(makeInfo("smoke", "Atom.Sprite.Smoke", "Atom.Sprite.Smoke.Description", "effect", 70,
			      {pFloat("detail", "Atom.Sprite.Detail", 3.5, 0.5, 16.0, 0.1),
			       pFloat("contrast", "Atom.Sprite.Contrast", 1.3, 0.2, 4.0, 0.05),
			       pInt("seed", "Atom.Sprite.Seed", 3, 0, 999)}),
		     [] { return std::make_unique<SmokeSprite>(); });

	registry.add(makeInfo("image", "Atom.Sprite.Image", "Atom.Sprite.Image.Description", "custom", 80, {}),
		     [] { return std::make_unique<ImageSprite>(); });
}

void registerTrails()
{
	auto &registry = TrailRegistry::instance();

	registry.add(makeInfo("streak", "Atom.Trail.Streak", "Atom.Trail.Streak.Description", "basic", 10,
			      {pFloat("taper", "Atom.Trail.Taper", 1.0, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<StreakTrail>(); });

	registry.add(makeInfo("ribbon", "Atom.Trail.Ribbon", "Atom.Trail.Ribbon.Description", "basic", 20,
			      {pFloat("softness", "Atom.Trail.Softness", 0.35, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<RibbonTrail>(); });

	registry.add(makeInfo("comet", "Atom.Trail.Comet", "Atom.Trail.Comet.Description", "effect", 30,
			      {pFloat("flare", "Atom.Trail.Flare", 0.6, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<CometTrail>(); });

	registry.add(makeInfo("sparkle", "Atom.Trail.Sparkle", "Atom.Trail.Sparkle.Description", "effect", 40,
			      {pFloat("scatter", "Atom.Trail.Scatter", 4.0, 0.0, 64.0, 0.5, "px"),
			       pFloat("density", "Atom.Trail.Density", 0.7, 0.05, 1.0, 0.01),
			       pFloat("twinkle", "Atom.Trail.Twinkle", 0.8, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<SparkleTrail>(); });
}

void registerFadePaths()
{
	auto &registry = FadePathRegistry::instance();

	registry.add(makeInfo("constant", "Atom.Fade.Constant", "Atom.Fade.Constant.Description", "basic", 10, {}),
		     [] { return std::make_unique<ConstantFade>(); });

	registry.add(makeInfo("linear_out", "Atom.Fade.LinearOut", "Atom.Fade.LinearOut.Description", "basic", 20, {}),
		     [] { return std::make_unique<LinearFade>(); });

	registry.add(makeInfo("ease_out", "Atom.Fade.EaseOut", "Atom.Fade.EaseOut.Description", "basic", 30,
			      {pFloat("power", "Atom.Fade.Power", 2.0, 0.1, 8.0, 0.05)}),
		     [] { return std::make_unique<EaseOutFade>(); });

	registry.add(makeInfo("hold", "Atom.Fade.Hold", "Atom.Fade.Hold.Description", "basic", 40,
			      {pFloat("hold", "Atom.Fade.HoldAmount", 0.6, 0.0, 0.95, 0.01)}),
		     [] { return std::make_unique<HoldFade>(); });

	registry.add(makeInfo("flare", "Atom.Fade.Flare", "Atom.Fade.Flare.Description", "effect", 50,
			      {pFloat("ignition", "Atom.Fade.Ignition", 0.08, 0.01, 0.5, 0.01),
			       pFloat("travel", "Atom.Fade.Travel", 0.45, 0.0, 1.0, 0.01),
			       pFloat("burnout", "Atom.Fade.Burnout", 0.75, 0.1, 0.99, 0.01)}),
		     [] { return std::make_unique<FlareFade>(); });

	registry.add(makeInfo("blink", "Atom.Fade.Blink", "Atom.Fade.Blink.Description", "effect", 60,
			      {pFloat("rate", "Atom.Fade.Rate", 6.0, 0.1, 60.0, 0.1),
			       pFloat("duty", "Atom.Fade.Duty", 0.5, 0.05, 0.95, 0.01),
			       pBool("decay", "Atom.Fade.Decay", true)}),
		     [] { return std::make_unique<BlinkFade>(); });

	registry.add(makeInfo("pulse", "Atom.Fade.Pulse", "Atom.Fade.Pulse.Description", "effect", 70,
			      {pFloat("cycles", "Atom.Fade.Cycles", 2.0, 0.1, 20.0, 0.1),
			       pFloat("depth", "Atom.Fade.Depth", 0.6, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<PulseFade>(); });

	registry.add(makeInfo("custom", "Atom.Fade.Custom", "Atom.Fade.Custom.Description", "custom", 80, {}),
		     [] { return std::make_unique<CustomFade>(); });
}

void registerFalloffs()
{
	auto &registry = FalloffRegistry::instance();

	registry.add(makeInfo("linear", "Atom.Falloff.Linear", "Atom.Falloff.Linear.Description", "basic", 10, {}),
		     [] { return std::make_unique<LinearFalloff>(); });
	registry.add(makeInfo("ease_in", "Atom.Falloff.EaseIn", "Atom.Falloff.EaseIn.Description", "basic", 20, {}),
		     [] { return std::make_unique<EaseInFalloff>(); });
	registry.add(makeInfo("ease_out", "Atom.Falloff.EaseOut", "Atom.Falloff.EaseOut.Description", "basic", 30, {}),
		     [] { return std::make_unique<EaseOutFalloff>(); });
	registry.add(makeInfo("smooth", "Atom.Falloff.Smooth", "Atom.Falloff.Smooth.Description", "basic", 40, {}),
		     [] { return std::make_unique<SmoothFalloff>(); });
	registry.add(makeInfo("exponential", "Atom.Falloff.Exponential", "Atom.Falloff.Exponential.Description",
			      "basic", 50, {pFloat("rate", "Atom.Falloff.Rate", 3.0, 0.05, 12.0, 0.05)}),
		     [] { return std::make_unique<ExponentialFalloff>(); });
	registry.add(makeInfo("custom", "Atom.Falloff.Custom", "Atom.Falloff.Custom.Description", "custom", 60, {}),
		     [] { return std::make_unique<CustomFalloff>(); });
}

void registerModulators()
{
	auto &registry = ModulatorRegistry::instance();

	registry.add(makeInfo("audio", "Atom.Modulator.Audio", "Atom.Modulator.Audio.Description", "basic", 10,
			      {pEnum("band", "Atom.Modulator.Band", "level",
				     {{"level", "Atom.Modulator.Band.Level"},
				      {"peak", "Atom.Modulator.Band.Peak"},
				      {"low", "Atom.Modulator.Band.Low"},
				      {"mid", "Atom.Modulator.Band.Mid"},
				      {"high", "Atom.Modulator.Band.High"}}),
			       pFloat("gain", "Atom.Modulator.Gain", 1.0, 0.0, 16.0, 0.05),
			       pFloat("floor", "Atom.Modulator.Floor", 0.0, 0.0, 1.0, 0.01),
			       pFloat("ceiling", "Atom.Modulator.Ceiling", 1.0, 0.0, 1.0, 0.01),
			       pBool("invert", "Atom.Modulator.Invert", false)}),
		     [] { return std::make_unique<AudioModulator>(); });

	registry.add(makeInfo("lfo", "Atom.Modulator.Lfo", "Atom.Modulator.Lfo.Description", "basic", 20,
			      {pFloat("rate", "Atom.Modulator.Rate", 0.5, 0.01, 30.0, 0.01, "Hz"),
			       pEnum("shape", "Atom.Modulator.Shape", "sine",
				     {{"sine", "Atom.Modulator.Shape.Sine"},
				      {"triangle", "Atom.Modulator.Shape.Triangle"},
				      {"square", "Atom.Modulator.Shape.Square"},
				      {"saw", "Atom.Modulator.Shape.Saw"}}),
			       pFloat("phase", "Atom.Modulator.Phase", 0.0, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<LfoModulator>(); });

	registry.add(makeInfo("noise", "Atom.Modulator.Noise", "Atom.Modulator.Noise.Description", "basic", 30,
			      {pFloat("rate", "Atom.Modulator.Rate", 1.0, 0.01, 30.0, 0.01, "Hz"),
			       pInt("seed", "Atom.Modulator.Seed", 1, 0, 9999)}),
		     [] { return std::make_unique<NoiseModulator>(); });

	registry.add(makeInfo("constant", "Atom.Modulator.Constant", "Atom.Modulator.Constant.Description", "basic", 40,
			      {pFloat("value", "Atom.Modulator.Value", 1.0, 0.0, 1.0, 0.01)}),
		     [] { return std::make_unique<ConstantModulator>(); });
}

void registerEndpoints()
{
	auto &registry = EndpointRegistry::instance();

	registry.add(makeInfo("none", "Atom.Endpoint.None", "Atom.Endpoint.None.Description", "basic", 10, {}),
		     [] { return std::make_unique<NoEndpoint>(); });
	registry.add(makeInfo("point", "Atom.Endpoint.PointMode", "Atom.Endpoint.PointMode.Description", "basic", 20,
			      {}),
		     [] { return std::make_unique<PointEndpoint>(); });
	registry.add(makeInfo("source", "Atom.Endpoint.SourceMode", "Atom.Endpoint.SourceMode.Description", "basic", 30,
			      {}),
		     [] { return std::make_unique<SourceEndpoint>(); });
}

} // namespace

void registerBuiltinModules()
{
	static bool registered = false;
	if (registered)
		return;
	registered = true;

	registerShapes();
	registerBehaviors();
	registerSprites();
	registerTrails();
	registerFadePaths();
	registerFalloffs();
	registerEndpoints();
	registerModulators();
}

namespace {

template<typename T> std::vector<std::pair<std::string, std::string>> itemsOf()
{
	std::vector<std::pair<std::string, std::string>> items;
	for (const auto &entry : Registry<T>::instance().entries())
		items.emplace_back(entry.info.id, entry.info.label);
	return items;
}

} // namespace

std::vector<std::pair<std::string, std::string>> registryEnumItems(const std::string &registryName)
{
	if (registryName == registries::kEmitterShape)
		return itemsOf<EmitterShape>();
	if (registryName == registries::kBehavior)
		return itemsOf<Behavior>();
	if (registryName == registries::kSprite)
		return itemsOf<SpriteShape>();
	if (registryName == registries::kTrail)
		return itemsOf<TrailStyle>();
	if (registryName == registries::kFadePath)
		return itemsOf<FadePath>();
	if (registryName == registries::kFalloff)
		return itemsOf<LifetimeFalloff>();
	if (registryName == registries::kEndpoint)
		return itemsOf<EndpointProvider>();
	if (registryName == registries::kModulator)
		return itemsOf<Modulator>();
	return {};
}

ParamSchema registrySchema(const std::string &registryName, const std::string &entryId)
{
	if (registryName == registries::kEmitterShape)
		return EmitterShapeRegistry::instance().schemaFor(entryId);
	if (registryName == registries::kBehavior)
		return BehaviorRegistry::instance().schemaFor(entryId);
	if (registryName == registries::kSprite)
		return SpriteRegistry::instance().schemaFor(entryId);
	if (registryName == registries::kTrail)
		return TrailRegistry::instance().schemaFor(entryId);
	if (registryName == registries::kFadePath)
		return FadePathRegistry::instance().schemaFor(entryId);
	if (registryName == registries::kFalloff)
		return FalloffRegistry::instance().schemaFor(entryId);
	if (registryName == registries::kEndpoint)
		return EndpointRegistry::instance().schemaFor(entryId);
	if (registryName == registries::kModulator)
		return ModulatorRegistry::instance().schemaFor(entryId);
	return {};
}

} // namespace atom
