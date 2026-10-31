// The graphics part's data (TPartGR::Read): layers of shapes and the views
// that show them. Shapes: RECT and ELPS (plain TShape), RRCT, LINE, ARC, BEZI
// (paths of Bezier vertices), and FRAM entries that place another part's
// frame among the shapes.
#ifndef GOBE_GR_PART_H
#define GOBE_GR_PART_H

#include <cstdint>
#include <string>
#include <vector>

#include "DocArchive.h"
#include "WPPart.h"

namespace gobe {

// TVertex::TVertex(TInputArchive&): the point, two control points, flags
struct Vertex {
	float point[2];
	float control1[2];
	float control2[2];
	uint8_t flags;
};

struct BezierPath {
	std::vector<Vertex> vertices;
	bool closed = false;
};

struct Shape {
	uint32_t kind = 0;			// 'RECT', 'RRCT', 'ELPS', 'LINE', 'ARC ', 'BEZI', 'FRAM';
								// in charts 'Rndr', 'Lgnd', 'Titl'
	// TShape (TTransObject: angle, origin, size; style; three u32)
	float angle = 0;
	float origin[2] = {0, 0};
	float size[2] = {0, 0};
	uint32_t style = 0;
	uint32_t values[3] = {};
	uint32_t lineValue = 0;		// LINE
	float radius[2] = {0, 0};	// RRCT corner radii
	float arcAngles[2] = {0, 0};	// ARC: start, sweep
	std::vector<BezierPath> paths;	// BEZI
	uint32_t frame[3] = {};		// FRAM: a frame pointer
	std::string text;			// chart title piece
	// kept for writing: RRCT's flag, ARC's last u32, a chart piece's u32
	// and its data from 'rnp+' / 'lgp+' / 'tlp+' to the closing signature
	bool rrctFlag = false;
	uint32_t arcValue = 0;
	uint32_t pieceValue = 0;
	std::vector<uint8_t> pieceData;
};

struct GRLayer {
	std::string name;
	std::vector<Shape> shapes;
};

struct GRView {
	std::string name;
	struct LayerInfo {
		uint32_t layer;
		bool flags[5];
	};
	std::vector<LayerInfo> layers;
};

struct GRPart {
	PartHeader header;
	// TPartGR::Read's grid (TGrid: a u32, a float, two bools), the size,
	// four u32 and a bool
	uint32_t grid = 0;
	float gridSpacing = 0;
	bool gridFlags[2] = {};
	float size[2] = {0, 0};			// the document size (presentations: the slide)
	uint32_t values[4] = {};
	bool flag = false;
	std::vector<GRLayer> layers;
	std::vector<GRView> views;
	// where 'GRAP' starts and 'GraP' ends in the part record
	size_t dataStart = 0, dataEnd = 0;
};

// `a` is positioned at the part record; reads up to and including 'GraP'.
void ReadGRPart(DocArchive& a, GRPart& out);

// TPartGR::Write's data, 'GRAP' to 'GraP', as ReadGRPart reads it; the
// archive's versions as the record was read with (FileFormatVersion,
// MajorUserVersion: Bezier paths are counted from 0x16, closed from 2).
std::vector<uint8_t> WriteGRData(const GRPart& part, int fileFormat, int majorVersion);

} // namespace gobe

#endif
