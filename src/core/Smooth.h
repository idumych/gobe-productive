#ifndef GOBE_SMOOTH_H
#define GOBE_SMOOTH_H

#include "GRPart.h"

namespace gobe {

// TPoint::PointToAngle: the angle in degrees (0 <= a < 360) from `from`
// to `to`. dx = to.x - from.x, dy = to.y - from.y. Same point: 0.
// dx == 0: 90 if dy > 0, else -90 (then made 0..360: 270).
// else a = atan(dy / dx) * 180 / 3.1415927; if dx < 0, a += 180.
// Result: fmod(a + 360, 360).
float PointToAngle(const float from[2], const float to[2]);

// TPoint::AngleToPoint: origin + (cos(rad) * radius[0], sin(rad) * radius[1]),
// rad = angle * 3.1415927 / 180.
void AngleToPoint(const float origin[2], float angle, const float radius[2], float out[2]);

// TVertex::SmoothControls: the control points of `v` (offsets from its
// point) for a smooth curve through prev and next (either may be null).
void SmoothControls(Vertex& v, const Vertex* prev, const Vertex* next);

// TGRBezigon::Recalc's smoothing: every vertex with (flags & 1) gets
// SmoothControls with its neighbours: prev = the vertex before (for the
// first: the last if path.closed, else null), next = the vertex after
// (for the last: the first if closed, else null). Then, if !path.closed,
// the first vertex's control1 and the last vertex's control2 are set to
// (0, 0). An empty path is left alone.
void SmoothPath(BezierPath& path);

} // namespace gobe

#endif
