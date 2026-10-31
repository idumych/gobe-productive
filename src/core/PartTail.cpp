#include "PartTail.h"

#include "DocWriter.h"

#include <cstring>

namespace gobe {

namespace {

void
ReadFramePtr(DocArchive& a)
{
	a.UInt32();
	a.UInt32();
	a.UInt32();
}


// skip to just after the next `sig`; the fallback for subclasses not read yet
void
SkipTo(DocArchive& a, uint32_t sig)
{
	const std::vector<uint8_t>& d = a.Data();
	uint8_t s[4] = {uint8_t(sig >> 24), uint8_t(sig >> 16), uint8_t(sig >> 8), uint8_t(sig)};
	for (size_t i = a.Position(); i + 4 <= d.size(); i++) {
		if (memcmp(&d[i], s, 4) == 0) {
			a.Seek(i);
			return;
		}
	}
	throw ArchiveError("signature not found", a.Position());
}


void
ReadFrame(DocArchive& a, uint32_t kind, Frame& f)
{
	// TShape -> TTransObject
	f.angle = a.Float();
	a.Point(f.origin);
	a.Point(f.extent);
	f.style = a.UInt32();
	for (uint32_t& v : f.shape)
		v = a.UInt32();
	// TFrame
	f.part = a.UInt32();
	f.container = a.UInt32();
	for (uint32_t& v : f.values)
		v = a.UInt32();
	size_t subclass = a.Position();
	struct Raw {
		DocArchive& a;
		Frame& f;
		size_t from;
		~Raw() { f.raw.assign(a.Data().begin() + from, a.Data().begin() + a.Position()); }
	} raw{a, f, subclass};

	switch (kind) {
		case Sig("WORD"):	// TWPFrame
			f.textAt = a.Position();
			f.textStart = a.UInt32();
			f.textLength = a.UInt32();
			if (a.FileFormatVersion() < 0x1e) {
				a.UInt32();
				a.UInt32();
			} else
				a.Float();
			a.Float();
			a.Float();
			if (a.FileFormatVersion() >= 0x1d)
				a.Float();
			break;
		case Sig("PAGE"):	// TPageFrame: the frames placed on this page
		{
			uint32_t count = a.UInt32();
			for (uint32_t i = 0; i < count; i++) {
				Frame::Pointer p;
				p.part = a.UInt32();
				p.portal = a.UInt32();
				p.frame = a.UInt32();
				f.children.push_back(p);
			}
			break;
		}
		case Sig("PROT"):	// TPagedRootFrame
		{
			float p[2];
			a.Point(p);
			break;
		}
		case Sig("BROT"):	// TBrowseRootFrame
			ReadFramePtr(a);
			ReadFramePtr(a);
			break;
		case Sig("SPRD"):	// TSSFrame
			for (uint16_t& v : f.cells)
				v = a.UInt16();
			a.Rect(f.sheetRect);
			break;
		case Sig("BITS"):	// TFrameIM: the image rectangle shown
		case Sig("GRFX"):	// TFrameGR: the drawing's rectangle shown
		case Sig("chrt"):	// TChartFrame: a TFrameGR
		case Sig("PRES"):
			a.Rect(f.sheetRect);
			break;
		case Sig("CLOK"):	// TClockFrame adds nothing
		case Sig("TBLE"):	// TTableFrame adds nothing
			break;
		default:
			f.exact = false;
			SkipTo(a, Sig("FraM"));
			return;
	}
}


void
ReadPortal(DocArchive& a, uint32_t kind, Portal& p)
{
	p.part = a.UInt32();
	for (uint32_t& v : p.values)	// +0x20, +0x24, +0x14
		v = a.UInt32();
	uint32_t frames = a.UInt32();
	for (uint32_t i = 0; i < frames; i++) {
		a.CheckSig(Sig("FRAM"));
		Frame f;
		ReadFrame(a, kind, f);
		if (!f.exact)
			p.exact = false;
		a.CheckSig(Sig("FraM"));
		p.frames.push_back(f);
	}
	size_t subclass = a.Position();
	struct Raw {
		DocArchive& a;
		Portal& p;
		size_t from;
		~Raw() { p.raw.assign(a.Data().begin() + from, a.Data().begin() + a.Position()); }
	} raw{a, p, subclass};
	switch (kind) {
		case Sig("WORD"):	// TWPPortal
			a.Float();
			break;
		case Sig("PAGE"):	// TPagePortal
			for (int i = 0; i < 4; i++)
				a.UInt32();
			ReadFramePtr(a);
			ReadFramePtr(a);
			a.Bool();
			a.Bool();
			break;
		case Sig("PROT"):
		case Sig("BROT"):	// TPart::ReadPortal: a plain TPortal
		case Sig("SPRD"):	// TSSPortal adds nothing
		case Sig("BITS"):	// TIMPortal
		case Sig("CLOK"):
		case Sig("TBLE"):
			break;
		case Sig("GRFX"):	// TGRPortal: the view (index into the part's views)
		case Sig("chrt"):
		case Sig("PRES"):
			p.view = a.UInt32();
			break;
		default:
			p.exact = false;
			SkipTo(a, Sig("PorT"));
			break;
	}
}


void
ReadTailAt(DocArchive& a, uint32_t kind, PartTail& out)
{
	size_t start = a.Position();
	a.CheckSig(Sig("REFT"));
	out.refNext = a.UInt32();
	uint32_t refs = a.UInt32();
	for (uint32_t i = 0; i < refs; i++) {
		Reference r;
		r.value14 = a.UInt32();
		r.flag0 = a.Bool();
		r.flag1 = a.Bool();
		r.name = a.String();
		if (a.Bool())
			r.selection = a.UInt32();
		r.valueC = a.UInt32();
		r.value10 = a.UInt32();
		out.refs.push_back(r);
	}
	a.CheckSig(Sig("RefT"));
	out.refsRaw.assign(a.Data().begin() + start, a.Data().begin() + a.Position());
	uint32_t portals = a.UInt32();
	for (uint32_t i = 0; i < portals; i++) {
		a.CheckSig(Sig("PORT"));
		Portal p;
		ReadPortal(a, kind, p);
		a.CheckSig(Sig("PorT"));
		out.portals.push_back(std::move(p));
	}
	a.CheckSig(Sig("ParT"));
}

} // namespace


bool
ReadPartTail(const std::vector<uint8_t>& record, uint32_t kind,
	const DocArchive& versions, PartTail& out)
{
	DocArchive a(record);
	a.CopyVersions(versions);
	for (size_t i = record.size() >= 8 ? record.size() - 8 : 0; i >= 8; i--) {
		if (memcmp(&record[i], "REFT", 4) != 0)
			continue;
		PartTail tail;
		try {
			a.Seek(i);
			ReadTailAt(a, kind, tail);
			if (a.Position() == record.size()) {
				tail.offset = i;
				out = std::move(tail);
				return true;
			}
		} catch (const ArchiveError&) {
		}
	}
	return false;
}

std::vector<uint8_t>
WritePartTail(const PartTail& tail, uint32_t kind)
{
	DocWriter w;
	if (tail.refsChanged) {
		// TRefTable::Write: each reference's index set first (+0xc)
		w.Sig(Sig("REFT"));
		w.UInt32(tail.refNext);
		w.UInt32(uint32_t(tail.refs.size()));
		for (size_t i = 0; i < tail.refs.size(); i++) {
			const Reference& r = tail.refs[i];	// TReference::Write
			w.UInt32(r.value14);
			w.Bool(r.flag0);
			w.Bool(r.flag1);
			w.String(r.name);
			w.Bool(r.selection != 0xffffffff);
			if (r.selection != 0xffffffff)
				w.UInt32(r.selection);
			w.UInt32(uint32_t(i));
			w.UInt32(r.value10);
		}
		w.Sig(Sig("RefT"));
	} else
		w.Bytes(tail.refsRaw);
	w.UInt32(uint32_t(tail.portals.size()));
	for (const Portal& p : tail.portals) {
		w.Sig(Sig("PORT"));
		w.UInt32(p.part);
		for (uint32_t v : p.values)
			w.UInt32(v);
		w.UInt32(uint32_t(p.frames.size()));
		for (const Frame& f : p.frames) {
			w.Sig(Sig("FRAM"));
			w.Float(f.angle);
			w.Point(f.origin);
			w.Point(f.extent);
			w.UInt32(f.style);
			for (uint32_t v : f.shape)
				w.UInt32(v);
			w.UInt32(f.part);
			w.UInt32(f.container);
			for (uint32_t v : f.values)
				w.UInt32(v);
			std::vector<uint8_t> raw = f.raw;
			if (kind == Sig("WORD") && raw.size() >= 8) {
				DocWriter range;
				range.UInt32(f.textStart);
				range.UInt32(f.textLength);
				std::copy(range.Data().begin(), range.Data().end(), raw.begin());
			} else if (kind == Sig("PAGE")) {
				DocWriter children;
				children.UInt32(uint32_t(f.children.size()));
				for (const Frame::Pointer& c : f.children) {
					children.UInt32(c.part);
					children.UInt32(c.portal);
					children.UInt32(c.frame);
				}
				raw = children.Data();
			} else if ((kind == Sig("GRFX") || kind == Sig("BITS") || kind == Sig("chrt")
					|| kind == Sig("PRES")) && raw.size() >= 16) {
				DocWriter rect;
				rect.Rect(f.sheetRect);
				std::copy(rect.Data().begin(), rect.Data().end(), raw.begin());
			}
			w.Bytes(raw);
			w.Sig(Sig("FraM"));
		}
		w.Bytes(p.raw);
		w.Sig(Sig("PorT"));
	}
	w.Sig(Sig("ParT"));
	return w.Data();
}

} // namespace gobe
