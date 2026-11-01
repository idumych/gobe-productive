// draw_smooth: PointToAngle, AngleToPoint, SmoothControls, SmoothPath
// Expected values from python3 with the same algorithm, e.g.:
//   python3 << 'PY'
//   import math
//   k=0.3926991
//   a=math.atan(1/1)*180/3.1415927; print(round((a+360)%360,1))  # -> 45.0
//   PY
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Smooth.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const char* what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what);
	}
}

static void
CheckFloat(float got, float expected, float tol, const char* what)
{
	Check(std::fabs(got - expected) < tol, what);
}

int
main()
{
	// ---- PointToAngle tests (7 checks) ----
	{
		// Expected: 0.0f
		float a = PointToAngle((float[2]){0, 0}, (float[2]){1, 0});
		CheckFloat(a, 0.0f, 1e-3f, "PointToAngle(0,0)->(1,0)");
	}
	{
		// Expected: 90.0f
		float a = PointToAngle((float[2]){0, 0}, (float[2]){0, 1});
		CheckFloat(a, 90.0f, 1e-3f, "PointToAngle(0,0)->(0,1)");
	}
	{
		// Expected: 270.0f
		float a = PointToAngle((float[2]){0, 0}, (float[2]){0, -1});
		CheckFloat(a, 270.0f, 1e-3f, "PointToAngle(0,0)->(0,-1)");
	}
	{
		// Expected: 180.0f
		float a = PointToAngle((float[2]){0, 0}, (float[2]){-1, 0});
		CheckFloat(a, 180.0f, 1e-3f, "PointToAngle(0,0)->(-1,0)");
	}
	{
		// python3: math.atan(1/1)*180/3.1415927 -> 44.9999993, rounds to 45.0
		// Expected: 45.0f
		float a = PointToAngle((float[2]){0, 0}, (float[2]){1, 1});
		CheckFloat(a, 45.0f, 1e-3f, "PointToAngle(0,0)->(1,1)");
	}
	{
		// python3: math.atan(-1/-1)*180/3.1415927+180 -> 224.9999993, rounds to 225.0
		// Expected: 225.0f
		float a = PointToAngle((float[2]){0, 0}, (float[2]){-1, -1});
		CheckFloat(a, 225.0f, 1e-3f, "PointToAngle(0,0)->(-1,-1)");
	}
	{
		// Same point -> 0
		float a = PointToAngle((float[2]){5, 5}, (float[2]){5, 5});
		CheckFloat(a, 0.0f, 1e-3f, "PointToAngle same point");
	}

	// ---- AngleToPoint test (1 check) ----
	{
		// python3: angle=90, rad=90*3.1415927/180, cos(rad)=~0, sin(rad)=~1
		// (1+cos*3, 2+sin*3) -> (0.9999999, ~5) within 1e-3 of (1,5)
		float out[2];
		AngleToPoint((float[2]){1, 2}, 90, (float[2]){3, 3}, out);
		CheckFloat(out[0], 1.0f, 1e-3f, "AngleToPoint(1,2) angle=90 radius=(3,3) x");
		CheckFloat(out[1], 5.0f, 1e-3f, "AngleToPoint(1,2) angle=90 radius=(3,3) y");
	}

	// ---- SmoothControls tests (5 checks) ----
	{
		// python3: prev=(-10,0), next=(10,0) -> aPrev=180, aNext=0, diff=180, dir=0
		// control2=(k*10, 0)=(3.926991, 0), control1=(k*10*cos(180), k*10*sin(180))
		//   =(-3.926991, -0.000000)
		Vertex v;
		std::memset(&v, 0, sizeof(v));
		v.point[0] = 0; v.point[1] = 0;
		Vertex prev = {{-10, 0}}, next = {{10, 0}};
		SmoothControls(v, &prev, &next);
		CheckFloat(v.control1[0], -3.926991f, 1e-3f, "SmoothControls prev=(−10,0) next=(10,0) control1.x");
		CheckFloat(v.control1[1], 0.0f, 1e-3f, "SmoothControls prev=(−10,0) next=(10,0) control1.y");
		CheckFloat(v.control2[0], 3.926991f, 1e-3f, "SmoothControls prev=(−10,0) next=(10,0) control2.x");
		CheckFloat(v.control2[1], 0.0f, 1e-3f, "SmoothControls prev=(−10,0) next=(10,0) control2.y");
	}

	{
		// python3: prev=null, next=(10,0) -> aPrev=180, aNext=0, diff=180, dir=0
		// control1=(0,0), control2=(3.926991, 0)
		Vertex v;
		std::memset(&v, 0, sizeof(v));
		v.point[0] = 0; v.point[1] = 0;
		Vertex next = {{10, 0}};
		SmoothControls(v, nullptr, &next);
		CheckFloat(v.control1[0], 0.0f, 1e-3f, "SmoothControls prev=null next=(10,0) control1.x");
		CheckFloat(v.control1[1], 0.0f, 1e-3f, "SmoothControls prev=null next=(10,0) control1.y");
		CheckFloat(v.control2[0], 3.926991f, 1e-3f, "SmoothControls prev=null next=(10,0) control2.x");
		CheckFloat(v.control2[1], 0.0f, 1e-3f, "SmoothControls prev=null next=(10,0) control2.y");
	}

	{
		// python3: prev=(-10,0), next=null -> aPrev=180, aNext=180, diff=0, dir=180
		// control1=(-3.926991, -0.000000), control2=(0,0)
		Vertex v;
		std::memset(&v, 0, sizeof(v));
		v.point[0] = 0; v.point[1] = 0;
		Vertex prev = {{-10, 0}};
		SmoothControls(v, &prev, nullptr);
		CheckFloat(v.control1[0], -3.926991f, 1e-3f, "SmoothControls prev=(-10,0) next=null control1.x");
		CheckFloat(v.control1[1], 0.0f, 1e-3f, "SmoothControls prev=(-10,0) next=null control1.y");
		CheckFloat(v.control2[0], 0.0f, 1e-3f, "SmoothControls prev=(-10,0) next=null control2.x");
		CheckFloat(v.control2[1], 0.0f, 1e-3f, "SmoothControls prev=(-10,0) next=null control2.y");
	}

	// ---- SmoothPath open test (3 checks) ----
	{
		// python3: open path (0,0),(10,10),(20,0) all flags=1
		// middle vertex control1=(-5.553604, -0.000000), control2=(5.553604, 0.000000)
		// first control1=(0,0), last control2=(0,0) after SmoothPath post-processing
		BezierPath path;
		path.closed = false;
		Vertex v0 = {{0, 0}, {0, 0}, {0, 0}, 1};
		Vertex v1 = {{10, 10}, {0, 0}, {0, 0}, 1};
		Vertex v2 = {{20, 0}, {0, 0}, {0, 0}, 1};
		path.vertices.push_back(v0);
		path.vertices.push_back(v1);
		path.vertices.push_back(v2);
		SmoothPath(path);
		// First vertex: control1 set to (0,0) by post-processing
		CheckFloat(path.vertices[0].control1[0], 0.0f, 1e-3f, "SmoothPath open first.control1.x");
		CheckFloat(path.vertices[2].control2[0], 0.0f, 1e-3f, "SmoothPath open last.control2.x");
		// Middle vertex: control1 and control2 from algorithm
		CheckFloat(path.vertices[1].control1[0], -5.553604f, 1e-3f,
			"SmoothPath open middle.control1.x");
		CheckFloat(path.vertices[1].control2[0], 5.553604f, 1e-3f,
			"SmoothPath open middle.control2.x");
	}

	// ---- SmoothPath closed test (2 checks) ----
	{
		// python3: closed path (0,0),(10,10),(20,0)
		// vertex 0 control1=(3.005589, -7.256133), control2=(-2.125272, 5.130861)
		BezierPath path;
		path.closed = true;
		Vertex v0 = {{0, 0}, {0, 0}, {0, 0}, 1};
		Vertex v1 = {{10, 10}, {0, 0}, {0, 0}, 1};
		Vertex v2 = {{20, 0}, {0, 0}, {0, 0}, 1};
		path.vertices.push_back(v0);
		path.vertices.push_back(v1);
		path.vertices.push_back(v2);
		SmoothPath(path);
		CheckFloat(path.vertices[0].control1[0], 3.005589f, 1e-3f,
			"SmoothPath closed first.control1.x");
		CheckFloat(path.vertices[0].control2[0], -2.125272f, 1e-3f,
			"SmoothPath closed first.control2.x");
	}

	// ---- Vertex without flags & 1 keeps its controls (1 check) ----
	{
		// Vertex with flags=0 (no smooth) should be left untouched
		BezierPath path;
		path.closed = false;
		Vertex v0 = {{0, 0}, {1, 2}, {3, 4}, 1};  // smooth
		Vertex v1 = {{10, 10}, {5, 6}, {7, 8}, 0}; // NOT smooth
		Vertex v2 = {{20, 0}, {9, 10}, {11, 12}, 1}; // smooth
		path.vertices.push_back(v0);
		path.vertices.push_back(v1);
		path.vertices.push_back(v2);
		SmoothPath(path);
		CheckFloat(path.vertices[1].control1[0], 5.0f, 1e-3f,
			"Unsmoothed vertex keeps original control1.x");
		CheckFloat(path.vertices[1].control2[0], 7.0f, 1e-3f,
			"Unsmoothed vertex keeps original control2.x");
	}

	printf("smooth ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
