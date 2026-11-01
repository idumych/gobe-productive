#include "Smooth.h"

#include <cmath>

namespace gobe {

static constexpr float kPi = 3.1415927f;
static constexpr float k = 0.3926991f;

float
PointToAngle(const float from[2], const float to[2])
{
	float dx = to[0] - from[0];
	float dy = to[1] - from[1];
	if (dx == 0 && dy == 0)
		return 0.0f;
	float a;
	if (dx == 0) {
		a = dy > 0 ? 90.0f : -90.0f;
	} else {
		a = std::atan(dy / dx) * 180.0f / kPi;
		if (dx < 0)
			a += 180.0f;
	}
	return std::fmod(a + 360.0f, 360.0f);
}

void
AngleToPoint(const float origin[2], float angle, const float radius[2], float out[2])
{
	float rad = angle * kPi / 180.0f;
	out[0] = origin[0] + std::cos(rad) * radius[0];
	out[1] = origin[1] + std::sin(rad) * radius[1];
}

void
SmoothControls(Vertex& v, const Vertex* prev, const Vertex* next)
{
	float aNext = 0, aPrev = 0;
	if (next)
		aNext = PointToAngle(v.point, next->point);
	if (prev)
		aPrev = PointToAngle(v.point, prev->point);
	if (!next)
		aNext = std::fmod(aPrev + 180 + 360, 360);
	if (!prev)
		aPrev = std::fmod(aNext + 180 + 360, 360);
	float diff = std::fmod(aNext - aPrev + 360, 360);
	float dir  = std::fmod(diff * 0.5f + aPrev + 90 + 360, 360);

	if (!next) {
		v.control2[0] = 0;
		v.control2[1] = 0;
	} else {
		float d = k * std::sqrt((v.point[0] - next->point[0]) * (v.point[0] - next->point[0])
			+ (v.point[1] - next->point[1]) * (v.point[1] - next->point[1]));
		const float origin[2] = {0, 0}, radius[2] = {d, d};
		AngleToPoint(origin, dir, radius, v.control2);
	}

	if (!prev) {
		v.control1[0] = 0;
		v.control1[1] = 0;
	} else {
		float d = k * std::sqrt((v.point[0] - prev->point[0]) * (v.point[0] - prev->point[0])
			+ (v.point[1] - prev->point[1]) * (v.point[1] - prev->point[1]));
		const float origin[2] = {0, 0}, radius[2] = {d, d};
		AngleToPoint(origin, dir + 180, radius, v.control1);
	}
}

void
SmoothPath(BezierPath& path)
{
	size_t n = path.vertices.size();
	if (n == 0)
		return;

	for (size_t i = 0; i < n; i++) {
		if (!(path.vertices[i].flags & 1))
			continue;

		const Vertex* prev = nullptr;
		const Vertex* next = nullptr;

		if (path.closed) {
			prev = &path.vertices[(i - 1 + n) % n];
			next = &path.vertices[(i + 1) % n];
		} else {
			if (i > 0)
				prev = &path.vertices[i - 1];
			if (i < n - 1)
				next = &path.vertices[i + 1];
		}

		SmoothControls(path.vertices[i], prev, next);
	}

	if (!path.closed) {
		path.vertices[0].control1[0] = 0;
		path.vertices[0].control1[1] = 0;
		path.vertices[n - 1].control2[0] = 0;
		path.vertices[n - 1].control2[1] = 0;
	}
}

} // namespace gobe
