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

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace atom {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

inline float deg2rad(float degrees)
{
	return degrees * (kPi / 180.0f);
}

inline float rad2deg(float radians)
{
	return radians * (180.0f / kPi);
}

inline float lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

inline float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

inline float saturate(float v)
{
	return clampf(v, 0.0f, 1.0f);
}

inline float smoothstep01(float t)
{
	t = saturate(t);
	return t * t * (3.0f - 2.0f * t);
}

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	Vec2() = default;
	Vec2(float x_, float y_) : x(x_), y(y_) {}

	Vec2 operator+(const Vec2 &o) const { return {x + o.x, y + o.y}; }
	Vec2 operator-(const Vec2 &o) const { return {x - o.x, y - o.y}; }
	Vec2 operator*(float s) const { return {x * s, y * s}; }
	Vec2 &operator+=(const Vec2 &o)
	{
		x += o.x;
		y += o.y;
		return *this;
	}
	Vec2 &operator*=(float s)
	{
		x *= s;
		y *= s;
		return *this;
	}

	bool operator==(const Vec2 &o) const { return x == o.x && y == o.y; }
	bool operator!=(const Vec2 &o) const { return !(*this == o); }

	float length() const { return std::sqrt(x * x + y * y); }
	float lengthSquared() const { return x * x + y * y; }

	Vec2 normalized() const
	{
		const float len = length();
		return len > 1e-6f ? Vec2{x / len, y / len} : Vec2{0.0f, 0.0f};
	}
};

inline Vec2 lerp(const Vec2 &a, const Vec2 &b, float t)
{
	return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)};
}

/// Unit vector for an angle in degrees. 0 degrees points right, 90 degrees points down
/// (screen space, matching OBS' top-left origin).
inline Vec2 angleToVector(float degrees)
{
	const float r = deg2rad(degrees);
	return {std::cos(r), std::sin(r)};
}

struct Color {
	float r = 1.0f;
	float g = 1.0f;
	float b = 1.0f;
	float a = 1.0f;

	Color() = default;
	Color(float r_, float g_, float b_, float a_) : r(r_), g(g_), b(b_), a(a_) {}

	/// Unpacks OBS' 0xAABBGGRR color layout (the layout used by obs_properties_add_color).
	static Color fromRGBA(uint32_t rgba)
	{
		return {static_cast<float>(rgba & 0xFF) / 255.0f, static_cast<float>((rgba >> 8) & 0xFF) / 255.0f,
			static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
			static_cast<float>((rgba >> 24) & 0xFF) / 255.0f};
	}

	uint32_t toRGBA() const
	{
		const auto q = [](float v) {
			return static_cast<uint32_t>(saturate(v) * 255.0f + 0.5f);
		};
		return q(r) | (q(g) << 8) | (q(b) << 16) | (q(a) << 24);
	}

	bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
	bool operator!=(const Color &o) const { return !(*this == o); }

	Color operator*(float s) const { return {r * s, g * s, b * s, a * s}; }
	Color modulate(const Color &o) const { return {r * o.r, g * o.g, b * o.b, a * o.a}; }
};

inline Color lerp(const Color &a, const Color &b, float t)
{
	return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

} // namespace atom
