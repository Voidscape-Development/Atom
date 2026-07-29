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

#include "atom-system.hpp"
#include "atom-noise.hpp"

namespace atom {

namespace {

/// Pixels per second squared at endpoint strength 1.0.
constexpr float kAttractScale = 500.0f;
/// Longest frame the simulation will integrate in one go, so a stall does not teleport atoms.
constexpr float kMaxTimeStep = 1.0f / 20.0f;
/// Fixed step used while prewarming.
constexpr float kPrewarmStep = 1.0f / 60.0f;
constexpr int kMaxPrewarmSteps = 1200;

} // namespace

AtomSystem::AtomSystem() : random_(0x1234567u)
{
	registerBuiltinModules();
	// A freshly constructed system must not prewarm: the caller is about to configure it, and
	// atoms from the default design would linger into the real configuration.
	applyConfig(EmitterConfig{}, false);
}

AtomSystem::~AtomSystem() = default;

void AtomSystem::configure(const EmitterConfig &config)
{
	applyConfig(config, true);
}

void AtomSystem::applyConfig(const EmitterConfig &config, bool allowPrewarm)
{
	const bool shapeChanged = config.emission.shapeId != config_.emission.shapeId;
	const bool layerCountChanged = config.design.layers.size() != config_.design.layers.size();

	config_ = config;

	if (config_.design.layers.empty())
		config_.design = AtomDesign::defaultDesign();

	if (!config_.emission.randomSeed)
		random_.setSeed(static_cast<uint32_t>(config_.emission.seed) * 2654435761u + 1u);

	rebuildModules();

	context_.width = width();
	context_.height = height();

	trailsEnabled_ = config_.design.anyTrails();
	if (!trailsEnabled_) {
		trails_.clear();
	} else {
		trails_.resize(atoms_.size());
	}

	if (layerCountChanged) {
		for (Atom &atom : atoms_)
			atom.layer = static_cast<uint16_t>(std::min<size_t>(
				atom.layer, config_.design.layers.empty() ? 0 : config_.design.layers.size() - 1));
	}

	if (shapeChanged)
		spawnAccumulator_ = 0.0f;

	const int maxAtoms = std::max(1, config_.emission.maxAtoms);
	while (static_cast<int>(atoms_.size()) > maxAtoms)
		retire(atoms_.size() - 1);

	if (allowPrewarm && config_.emission.prewarm && atoms_.empty())
		prewarm(context_);
}

void AtomSystem::rebuildModules()
{
	shape_ = EmitterShapeRegistry::instance().create(config_.emission.shapeId);
	if (!shape_)
		shape_ = EmitterShapeRegistry::instance().create("box");
	if (shape_) {
		ParamBag params = config_.emission.shapeParams;
		params.applyDefaults(EmitterShapeRegistry::instance().schemaFor(config_.emission.shapeId));
		shape_->configure(params);
	}

	endpoint_ = EndpointRegistry::instance().create(config_.physics.endpoint.modeId);
	if (!endpoint_)
		endpoint_ = EndpointRegistry::instance().create("none");

	behaviors_.clear();
	for (const BehaviorInstance &instance : config_.physics.extras) {
		if (!instance.enabled)
			continue;
		std::unique_ptr<Behavior> behavior = BehaviorRegistry::instance().create(instance.id);
		if (!behavior)
			continue;
		ParamBag params = instance.params;
		params.applyDefaults(BehaviorRegistry::instance().schemaFor(instance.id));
		behavior->configure(params);
		behaviors_.push_back(std::move(behavior));
	}

	evaluator_.rebuild(config_.design, config_.physics);
}

bool AtomSystem::needsHostTarget() const
{
	return endpoint_ && endpoint_->needsHostTarget();
}

void AtomSystem::reset()
{
	atoms_.clear();
	trails_.clear();
	spawnAccumulator_ = 0.0f;
	burstTimer_ = 0.0f;
	time_ = 0.0f;
}

void AtomSystem::retire(size_t index)
{
	if (index >= atoms_.size())
		return;

	const size_t last = atoms_.size() - 1;
	if (index != last)
		atoms_[index] = atoms_[last];
	atoms_.pop_back();

	if (trails_.size() > last) {
		if (index != last)
			trails_[index] = trails_[last];
		trails_.pop_back();
	}
}

Vec2 AtomSystem::endpointFor(const Atom &atom) const
{
	const float scatter = config_.physics.endpoint.scatter;
	if (scatter <= 0.0f)
		return endpointTarget_;

	return endpointTarget_ + Vec2{(atom.randomA - 0.5f) * 2.0f * scatter, (atom.randomB - 0.5f) * 2.0f * scatter};
}

void AtomSystem::spawnOne(const SimContext &context)
{
	if (!shape_)
		return;

	const EmissionConfig &emission = config_.emission;
	const PhysicsConfig &physics = config_.physics;
	const OffsetConfig &offset = physics.offset;

	const SpawnSample sample = shape_->sample(random_, context);

	Atom atom;
	atom.pos = sample.position;
	if (offset.position > 0.0f)
		atom.pos += random_.insideUnitCircle() * offset.position;
	atom.spawnPos = atom.pos;

	// Direction.
	float angle = physics.emitAngle;
	const Vec2 fromCenter = (atom.pos - context.center());
	if (emission.directionModeId == "shape_normal") {
		const Vec2 normal = sample.normal.lengthSquared() > 0.0f ? sample.normal : fromCenter.normalized();
		angle = rad2deg(std::atan2(normal.y, normal.x));
	} else if (emission.directionModeId == "outward") {
		const Vec2 direction = fromCenter.lengthSquared() > 0.0f ? fromCenter.normalized()
									 : random_.onUnitCircle();
		angle = rad2deg(std::atan2(direction.y, direction.x));
	} else if (emission.directionModeId == "inward") {
		const Vec2 direction = (context.center() - atom.pos).normalized();
		angle = rad2deg(std::atan2(direction.y, direction.x));
	} else if (emission.directionModeId == "random") {
		angle = random_.next() * 360.0f;
	} else if (emission.directionModeId == "endpoint" && hasEndpointTarget_) {
		const Vec2 direction = (endpointTarget_ - atom.pos).normalized();
		angle = rad2deg(std::atan2(direction.y, direction.x));
	}

	angle += random_.nextSigned() * physics.emitSpread * 0.5f;
	if (offset.angle > 0.0f)
		angle += random_.nextSigned() * offset.angle;

	float speed = physics.initialSpeed;
	if (offset.speed > 0.0f)
		speed *= std::max(0.0f, 1.0f + random_.nextSigned() * offset.speed);
	atom.vel = angleToVector(angle) * speed;

	// Lifetime.
	atom.life = std::max(0.05f, physics.lifetime * (1.0f + random_.nextSigned() * offset.lifetime));
	if (offset.spawnPhase > 0.0f)
		atom.age = random_.next() * atom.life * saturate(offset.spawnPhase);

	// Per-atom randoms, used by behaviours, trails and fade paths.
	atom.randomA = random_.next();
	atom.randomB = random_.next();
	atom.randomC = random_.next();

	// Layer and size.
	const size_t layerIndex = config_.design.pickLayer(random_.next());
	atom.layer = static_cast<uint16_t>(layerIndex);
	const AtomLayer &layer = config_.design.layers[layerIndex];
	atom.size = std::max(0.1f, layer.size.base * (1.0f + random_.nextSigned() * offset.size));

	// Rotation.
	atom.rotation = random_.next() * 360.0f * saturate(offset.rotation);
	atom.rotationVelocity = physics.spin * (1.0f + random_.nextSigned() * offset.rotation);

	// Colour scatter.
	atom.hueShift = random_.nextSigned() * offset.hue * 0.5f;
	if (offset.brightness > 0.0f) {
		const float brightness = std::max(0.0f, 1.0f + random_.nextSigned() * offset.brightness);
		atom.tint.r *= brightness;
		atom.tint.g *= brightness;
		atom.tint.b *= brightness;
	}
	if (offset.alpha > 0.0f)
		atom.tint.a *= std::max(0.0f, 1.0f - random_.next() * offset.alpha);

	atoms_.push_back(atom);
	if (trailsEnabled_) {
		trails_.emplace_back();
		trails_.back().push(atom.pos);
	}
}

void AtomSystem::spawn(size_t count, const SimContext &context)
{
	const size_t maxAtoms = static_cast<size_t>(std::max(1, config_.emission.maxAtoms));
	for (size_t i = 0; i < count && atoms_.size() < maxAtoms; ++i)
		spawnOne(context);
}

void AtomSystem::burst(int count)
{
	spawn(static_cast<size_t>(std::max(0, count)), context_);
}

void AtomSystem::applyForces(Atom &atom, float dt, const SimContext &context)
{
	const PhysicsConfig &physics = config_.physics;

	if (physics.gravity != 0.0f)
		atom.vel += angleToVector(physics.gravityAngle) * (physics.gravity * dt);

	if (physics.turbulence > 0.0f) {
		const float scale = std::max(1.0f, physics.turbulenceScale);
		const Vec2 flow = noise::flow(atom.pos.x / scale, atom.pos.y / scale,
					      context.time * physics.turbulenceSpeed + atom.randomA);
		atom.vel += flow * (physics.turbulence * dt);
	}

	if (physics.drag > 0.0f) {
		// Exponential damping keeps the result frame-rate independent.
		const float damping = std::exp(-physics.drag * dt);
		atom.vel *= damping;
	}

	for (const std::unique_ptr<Behavior> &behavior : behaviors_)
		behavior->apply(atom, dt, context);
}

void AtomSystem::applyEndpoint(Atom &atom, float dt)
{
	if (!hasEndpointTarget_)
		return;

	const EndpointConfig &endpoint = config_.physics.endpoint;
	const Vec2 target = endpointFor(atom);
	const Vec2 delta = target - atom.pos;
	const float distance = delta.length();

	if (endpoint.arrivalId == "arrive") {
		// Blend towards the target so the atom lands exactly as its life runs out.
		const float remaining = std::max(1e-3f, atom.life - atom.age);
		const float blend = saturate(dt / remaining * std::max(0.0f, endpoint.strength));
		atom.pos = lerp(atom.pos, target, blend);
	} else if (endpoint.arrivalId == "orbit") {
		if (distance > 1e-3f) {
			const Vec2 radial = delta * (1.0f / distance);
			const Vec2 tangent{-radial.y, radial.x};
			atom.vel += tangent * (kAttractScale * endpoint.strength * dt);
			atom.vel += radial * (kAttractScale * 0.25f * endpoint.strength * dt);
		}
	} else {
		if (distance > 1e-3f)
			atom.vel += delta * (1.0f / distance) * (kAttractScale * endpoint.strength * dt);
	}

	if (endpoint.killOnArrival && distance <= endpoint.arriveRadius)
		atom.expired = true;
}

void AtomSystem::recordTrail(size_t index, float dt)
{
	if (index >= trails_.size() || index >= atoms_.size())
		return;

	const Atom &atom = atoms_[index];
	const AtomLayer *layer = evaluator_.layerFor(atom);
	if (!layer || !layer->trail.enabled)
		return;

	TrailHistory &history = trails_[index];
	const int segments = std::max(2, std::min<int>(layer->trail.segments, static_cast<int>(kMaxTrailPoints)));
	const float interval = std::max(1.0f / 240.0f, layer->trail.length / static_cast<float>(segments));

	history.sampleTimer += dt;
	while (history.sampleTimer >= interval) {
		history.sampleTimer -= interval;
		history.push(atom.pos);
	}
}

void AtomSystem::update(float dt, SimContext context)
{
	if (dt <= 0.0f)
		return;

	dt = std::min(dt, kMaxTimeStep);
	time_ += dt;

	context.width = width();
	context.height = height();
	context.time = time_;
	context_ = context;

	hasEndpointTarget_ = endpoint_ && endpoint_->resolve(context_, config_.physics.endpoint, endpointTarget_);

	// Emission.
	if (emitting_) {
		const EmissionConfig &emission = config_.emission;
		if (emission.modeId != "burst") {
			spawnAccumulator_ += emission.rate * dt;
			const size_t due = static_cast<size_t>(spawnAccumulator_);
			if (due > 0) {
				spawnAccumulator_ -= static_cast<float>(due);
				spawn(due, context_);
			}
		}
		if (emission.modeId != "continuous") {
			burstTimer_ -= dt;
			if (burstTimer_ <= 0.0f) {
				burstTimer_ += std::max(0.05f, emission.burstInterval);
				spawn(static_cast<size_t>(std::max(0, emission.burstCount)), context_);
			}
		}
	}

	// Integration.
	for (size_t i = 0; i < atoms_.size();) {
		Atom &atom = atoms_[i];

		applyForces(atom, dt, context_);
		applyEndpoint(atom, dt);

		atom.pos += atom.vel * dt;
		atom.rotation += atom.rotationVelocity * dt;
		if (config_.physics.alignToVelocity && atom.vel.lengthSquared() > 1e-4f)
			atom.rotation = rad2deg(std::atan2(atom.vel.y, atom.vel.x));

		atom.age += dt;

		if (atom.expired || atom.age >= atom.life) {
			retire(i);
			continue;
		}

		recordTrail(i, dt);
		++i;
	}
}

void AtomSystem::prewarm(const SimContext &context)
{
	const float duration =
		std::min(config_.physics.lifetime * 1.2f, static_cast<float>(kMaxPrewarmSteps) * kPrewarmStep);
	const int steps = std::max(1, static_cast<int>(duration / kPrewarmStep));

	SimContext local = context;
	for (int i = 0; i < steps; ++i)
		update(kPrewarmStep, local);
}

} // namespace atom
