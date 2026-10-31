#include "GRPart.h"

#include "DocWriter.h"

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
			s.rrctFlag = a.Bool();
			break;
		case Sig("LINE"):
			ReadShapeBase(a, s);
			s.lineValue = a.UInt32();
			break;
		case Sig("ARC "):
			ReadShapeBase(a, s);
			s.arcAngles[0] = a.Float();
			s.arcAngles[1] = a.Float();
			s.arcValue = a.UInt32();
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
			s.pieceValue = a.UInt32();
			// 'rnp+' (T3DRenderer: camera, lights), 'lgp+', 'tlp+' (title
			// text, position) up to the matching '-' signature
			size_t start = a.Position();
			uint32_t open = a.UInt32();
			if ((open & 0xff) != '+')
				throw ArchiveError("chart piece without data", a.Position() - 4);
			if (open == Sig("tlp+")) {
				s.text = a.String();
				a.Point(s.radius);	// the title's position
				a.CheckSig(Sig("tlp-"));
			} else {
				uint32_t close = (open & ~0xffu) | '-';
				const std::vector<uint8_t>& d = a.Data();
				size_t at = a.Position();
				while (at + 4 <= d.size() && (uint32_t(d[at]) << 24 | d[at + 1] << 16
						| d[at + 2] << 8 | d[at + 3]) != close)
					at++;
				if (at + 4 > d.size())
					throw ArchiveError("chart piece not closed", a.Position());
				a.Seek(at + 4);
			}
			s.pieceData.assign(a.Data().begin() + long(start), a.Data().begin() + long(a.Position()));
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

	out.dataStart = a.Position();
	a.CheckSig(Sig("GRAP"));
	out.grid = a.UInt32();		// TGrid
	out.gridSpacing = a.Float();
	out.gridFlags[0] = a.Bool();
	out.gridFlags[1] = a.Bool();
	a.Point(out.size);		// the drawing's (slide's) size
	for (uint32_t& v : out.values)
		v = a.UInt32();
	out.flag = a.Bool();

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
	out.dataEnd = a.Position();
}


namespace {

void
WriteShapeBase(DocWriter& w, const Shape& s)
{
	w.Float(s.angle);
	w.Point(s.origin);
	w.Point(s.size);
	w.UInt32(s.style);
	for (uint32_t v : s.values)
		w.UInt32(v);
}


void
WriteBezier(DocWriter& w, const BezierPath& path, int majorVersion)
{
	w.UInt32(uint32_t(path.vertices.size()));
	for (const Vertex& v : path.vertices) {
		w.Point(v.point);
		w.Point(v.control1);
		w.Point(v.control2);
		w.UInt8(v.flags);
	}
	if (majorVersion >= 2)
		w.Bool(path.closed);
}

} // namespace


std::vector<uint8_t>
WriteGRData(const GRPart& part, int fileFormat, int majorVersion)
{
	DocWriter w;
	w.Sig(Sig("GRAP"));
	w.UInt32(part.grid);
	w.Float(part.gridSpacing);
	w.Bool(part.gridFlags[0]);
	w.Bool(part.gridFlags[1]);
	w.Point(part.size);
	for (uint32_t v : part.values)
		w.UInt32(v);
	w.Bool(part.flag);
	w.Sig(Sig("lyr+"));
	w.UInt32(uint32_t(part.layers.size()));
	for (const GRLayer& layer : part.layers) {
		w.String(layer.name);
		w.UInt32(uint32_t(layer.shapes.size()));
		for (const Shape& s : layer.shapes) {
			w.Sig(s.kind);
			switch (s.kind) {
				case Sig("FRAM"):
					for (uint32_t v : s.frame)
						w.UInt32(v);
					break;
				case Sig("RECT"):
				case Sig("ELPS"):
					WriteShapeBase(w, s);
					break;
				case Sig("RRCT"):
					WriteShapeBase(w, s);
					w.Point(s.radius);
					w.Bool(s.rrctFlag);
					break;
				case Sig("LINE"):
					WriteShapeBase(w, s);
					w.UInt32(s.lineValue);
					break;
				case Sig("ARC "):
					WriteShapeBase(w, s);
					w.Float(s.arcAngles[0]);
					w.Float(s.arcAngles[1]);
					w.UInt32(s.arcValue);
					break;
				case Sig("BEZI"):
					WriteShapeBase(w, s);
					if (fileFormat < 0x16) {
						if (!s.paths.empty())
							WriteBezier(w, s.paths[0], majorVersion);
					} else {
						w.UInt32(uint32_t(s.paths.size()));
						for (const BezierPath& path : s.paths)
							WriteBezier(w, path, majorVersion);
					}
					break;
				default:	// chart pieces
					WriteShapeBase(w, s);
					w.UInt32(s.pieceValue);
					w.Bytes(s.pieceData);
					break;
			}
		}
	}
	w.Sig(Sig("lyr-"));
	w.Sig(Sig("vew+"));
	w.UInt32(uint32_t(part.views.size()));
	for (const GRView& view : part.views) {
		w.String(view.name);
		w.UInt32(uint32_t(view.layers.size()));
		for (const GRView::LayerInfo& info : view.layers) {
			w.UInt32(info.layer);
			for (bool f : info.flags)
				w.Bool(f);
		}
	}
	w.Sig(Sig("vew-"));
	w.Sig(Sig("GraP"));
	return w.Data();
}

} // namespace gobe
