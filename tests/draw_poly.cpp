// draw_poly: WriteToolbarShape round-trips (inverse of ParseToolbarShape)
#include <cstdio>
#include <cstring>

#include "Drawing.h"

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

int
main()
{
	// ---- 1. 3-vertex open path round-trip (9 checks) ----
	{
		BezierPath p;
		p.closed = false;
		Vertex v0, v1, v2;
		v0.point[0] = 1.0f;  v0.point[1] = -12.5f;
		v0.control1[0] = 0.1f;  v0.control1[1] = 0.0f;
		v0.control2[0] = -0.1f;  v0.control2[1] = 0.0f;
		v0.flags = 0;

		v1.point[0] = 10.5f;  v1.point[1] = 20.3f;
		v1.control1[0] = 0.0f;  v1.control1[1] = 1e-3f;
		v1.control2[0] = 0.0f;  v1.control2[1] = -1e-3f;
		v1.flags = 1;

		v2.point[0] = 2.0f;  v2.point[1] = 100.0f;
		v2.control1[0] = 0.0f;  v2.control1[1] = 0.0f;
		v2.control2[0] = 0.0f;  v2.control2[1] = 0.0f;
		v2.flags = 3;

		p.vertices.push_back(v0);
		p.vertices.push_back(v1);
		p.vertices.push_back(v2);

		std::vector<uint8_t> bytes = WriteToolbarShape(p);
		BezierPath parsed;
		Check(ParseToolbarShape(bytes, parsed), "3-vertex open round-trip parses");
		Check(parsed.vertices.size() == 3, "3-vertex open round-trip count");
		Check(parsed.closed == false, "3-vertex open round-trip closed");
		Check(parsed.vertices[0].flags == 0, "v0 flags == 0");
		Check(parsed.vertices[1].flags == 1, "v1 flags == 1");
		Check(parsed.vertices[2].flags == 3, "v2 flags == 3");
		Check(parsed.vertices[0].point[0] == v0.point[0], "v0 point[0]");
		Check(parsed.vertices[1].control1[1] == v1.control1[1], "v1 control1[1] (1e-3)");
		Check(parsed.vertices[2].point[1] == v2.point[1], "v2 point[1] (100)");
	}

	// ---- 2. Same path closed (4 checks) ----
	{
		BezierPath p;
		p.closed = true;
		Vertex v0, v1;
		v0.point[0] = 0; v0.point[1] = 0;
		v0.control1[0] = 1; v0.control1[1] = 0;
		v0.control2[0] = 0; v0.control2[1] = 0;
		v0.flags = 1;
		v1.point[0] = 10; v1.point[1] = 10;
		v1.control1[0] = 0; v1.control1[1] = 0;
		v1.control2[0] = -1; v1.control2[1] = 0;
		v1.flags = 1;
		p.vertices.push_back(v0);
		p.vertices.push_back(v1);

		std::vector<uint8_t> bytes = WriteToolbarShape(p);
		BezierPath parsed;
		Check(ParseToolbarShape(bytes, parsed), "2-vertex closed round-trip parses");
		Check(parsed.closed == true, "closed comes back true");
		Check(parsed.vertices[0].point[0] == 0, "closed v0 point[0]");
		Check(parsed.vertices[1].control2[0] == -1, "closed v1 control2[0] (-1)");
	}

	// ---- 3. Byte order (4 checks) ----
	{
		BezierPath p;
		p.closed = false;
		Vertex v0, v1;
		v0.point[0] = 1; v0.point[1] = 2;
		v0.control1[0] = 0; v0.control1[1] = 0;
		v0.control2[0] = 0; v0.control2[1] = 0;
		v0.flags = 0;
		v1.point[0] = 3; v1.point[1] = 4;
		v1.control1[0] = 0; v1.control1[1] = 0;
		v1.control2[0] = 0; v1.control2[1] = 0;
		v1.flags = 0;
		p.vertices.push_back(v0);
		p.vertices.push_back(v1);

		std::vector<uint8_t> bytes = WriteToolbarShape(p);
		Check(bytes[0] == 2 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0,
			"count bytes (2,0,0,0) LE");
		Check(bytes[4] == 0 && bytes[5] == 0 && bytes[6] == 0 && bytes[7] == 0,
			"closed bytes (0,0,0,0) LE");
	}

	// ---- 4. 1-vertex path returns false (1 check) ----
	{
		BezierPath p;
		p.closed = false;
		Vertex v;
		v.point[0] = 5; v.point[1] = 5;
		v.control1[0] = 0; v.control1[1] = 0;
		v.control2[0] = 0; v.control2[1] = 0;
		v.flags = 0;
		p.vertices.push_back(v);

		std::vector<uint8_t> bytes = WriteToolbarShape(p);
		BezierPath parsed;
		Check(ParseToolbarShape(bytes, parsed) == false, "1-vertex path parses false");
	}

	// ---- 5. 4-vertex closed path round-trip (4 checks) ----
	{
		BezierPath p;
		p.closed = true;
		for (int i = 0; i < 4; i++) {
			Vertex v;
			v.point[0] = float(i * 5);  v.point[1] = float(i * 5 + 1);
			v.control1[0] = v.control1[1] = 0;
			v.control2[0] = v.control2[1] = 0;
			v.flags = uint8_t(i);
			p.vertices.push_back(v);
		}

		std::vector<uint8_t> bytes = WriteToolbarShape(p);
		BezierPath parsed;
		Check(ParseToolbarShape(bytes, parsed), "4-vertex closed round-trip parses");
		Check(parsed.vertices.size() == 4, "4-vertex closed round-trip count");
		Check(parsed.closed == true, "4-vertex closed closed");
		Check(bytes[0] == 4 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0,
			"4-vertex count bytes (4,0,0,0) LE");
	}

	printf("poly ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
