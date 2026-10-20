#include "GRPart.h"

namespace gobe {

namespace {

void
ReadShapeBase(DocArchive& a, Shape& s)
{
	s.angle = a.Float();
	a.Point(s.origin);
	a.Point(s.size);
	s.style = a.UInt32();
	for (uint32_t& v : s.values)
		v = a.UInt32();
}


BezierPath
ReadBezier(DocArchive& a)
{
	BezierPath path;
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		Vertex v;
		a.Point(v.point);
		a.Point(v.control1);
		a.Point(v.control2);
		v.flags = a.UInt8();
		path.vertices.push_back(v);
	}
	if (a.MajorUserVersion() >= 2)
		path.closed = a.Bool();
	return path;
}


// TPartGR::ReadShape / CreateShape
Shape
ReadShape(DocArchive& a)
{
	Shape s;
	s.kind = a.UInt32();
	switch (s.kind) {
		case Sig("FRAM"):
			for (uint32_t& v : s.frame)
				v = a.UInt32();
			break;
		case Sig("RECT"):
		case Sig("ELPS"):
			ReadShapeBase(a, s);
			break;
		case Sig("RRCT"):	// rounded rectangle: corner radii and a flag
			ReadShapeBase(a, s);
			a.Point(s.radius);
			a.Bool();
			break;
		case Sig("LINE"):
			ReadShapeBase(a, s);
			s.lineValue = a.UInt32();
			break;
		case Sig("ARC "):
			ReadShapeBase(a, s);
			s.arcAngles[0] = a.Float();
			s.arcAngles[1] = a.Float();
			a.UInt32();
			break;
		case Sig("BEZI"):
			ReadShapeBase(a, s);
			if (a.FileFormatVersion() < 0x16) {
				s.paths.push_back(ReadBezier(a));
			} else {
				uint32_t count = a.UInt32();
				for (uint32_t i = 0; i < count; i++)
					s.paths.push_back(ReadBezier(a));
			}
			break;
		case Sig("Rndr"):	// chart pieces (TChartPiece: a TShape + u32)
		case Sig("Lgnd"):
		case Sig("Titl"): {
			ReadShapeBase(a, s);
			a.UInt32();
			// 'rnp+' (T3DRenderer: camera, lights), 'lgp+', 'tlp+' (title
			// text, position) up to the matching '-' signature
			uint32_t open = a.UInt32();
			if ((open & 0xff) != '+')
				throw ArchiveError("chart piece without data", a.Position() - 4);
			if (open == Sig("tlp+")) {
				s.text = a.String();
				a.Point(s.radius);	// the title's position
				a.CheckSig(Sig("tlp-"));
				break;
			}
			uint32_t close = (open & ~0xffu) | '-';
			const std::vector<uint8_t>& d = a.Data();
			size_t at = a.Position();
			while (at + 4 <= d.size() && (uint32_t(d[at]) << 24 | d[at + 1] << 16
					| d[at + 2] << 8 | d[at + 3]) != close)
				at++;
			if (at + 4 > d.size())
				throw ArchiveError("chart piece not closed", a.Position());
			a.Seek(at + 4);
			break;
		}
		default:
			throw ArchiveError("unknown shape kind", a.Position() - 4);
	}
	return s;
}

} // namespace


void
ReadGRPart(DocArchive& a, GRPart& out)
{
	a.CheckSig(Sig("PART"));
	a.UInt32();
	ReadPartHeader(a, out.header);

	a.CheckSig(Sig("GRAP"));
	a.UInt32();		// TGrid
	a.Float();
	a.Bool();
	a.Bool();
	a.Point(out.size);		// the drawing's (slide's) size
	for (int i = 0; i < 4; i++)
		a.UInt32();
	a.Bool();

	a.CheckSig(Sig("lyr+"));
	uint32_t layers = a.UInt32();
	for (uint32_t i = 0; i < layers; i++) {
		GRLayer layer;
		layer.name = a.String();
		uint32_t shapes = a.UInt32();	// TShapeTable::Read
		for (uint32_t s = 0; s < shapes; s++)
			layer.shapes.push_back(ReadShape(a));
		out.layers.push_back(std::move(layer));
	}
	a.CheckSig(Sig("lyr-"));

	a.CheckSig(Sig("vew+"));
	uint32_t views = a.UInt32();
	for (uint32_t i = 0; i < views; i++) {
		GRView view;
		view.name = a.String();
		uint32_t infos = a.UInt32();
		for (uint32_t k = 0; k < infos; k++) {
			GRView::LayerInfo info;
			info.layer = a.UInt32();
			for (bool& f : info.flags)
				f = a.Bool();
			view.layers.push_back(info);
		}
		out.views.push_back(std::move(view));
	}
	a.CheckSig(Sig("vew-"));
	a.CheckSig(Sig("GraP"));
}

} // namespace gobe
