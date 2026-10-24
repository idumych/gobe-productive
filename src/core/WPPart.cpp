#include "WPPart.h"

#include "DocWriter.h"

#include <algorithm>
#include "Formula.h"

namespace gobe {

void
ReadPartHeader(DocArchive& a, PartHeader& out)
{
	out.message = a.Message();
	for (uint32_t& v : out.values)
		v = a.UInt32();
	out.selection = a.UInt32();		// TSelPtr::Read
}


static void
ReadFramePtr(DocArchive& a, uint32_t frame[3])
{
	for (int i = 0; i < 3; i++)
		frame[i] = a.UInt32();
}


static void
ReadParagraph(DocArchive& a, WPParagraph& p)
{
	p.start = a.UInt32();
	p.length = a.UInt32();
	p.value12 = a.UInt16();
	p.parStyle = a.UInt32();
	uint32_t runs = a.UInt32();
	for (uint32_t i = 0; i < runs; i++) {
		WPStyleRun r;
		r.position = a.UInt32();
		r.style = a.UInt32();
		p.runs.push_back(r);
	}
	uint32_t misc = a.UInt32();
	for (uint32_t i = 0; i < misc; i++) {
		WPMiscInfo m{};
		m.kind = a.UInt32();
		m.position = a.UInt32();	// TMiscInfo::TMiscInfo(TInputArchive&)
		size_t payload = a.Position();
		switch (m.kind) {
			case Sig("FRAM"):
				ReadFramePtr(a, m.frame);
				break;
			case Sig("FORM"):
			{
				// TMiscFormulaInfo: an expression, a u32, three bools
				ReadExpression(a, m.expression);
				m.text = m.expression.text;
				m.command = a.UInt32();
				m.isStatic = a.Bool();
				m.displayResult = a.Bool();
				m.displayFormula = a.Bool();
				break;
			}
			case Sig("FREF"):
				ReadFramePtr(a, m.frame);
				m.endnote = a.Bool();
				m.autoNumber = a.Bool();
				m.number = a.UInt32();
				m.text = a.String();
				break;
			case Sig("FMRK"):
			case Sig("LINE"):
			case Sig("PAGE"):
				break;
			default:
				throw ArchiveError("unknown paragraph info kind", a.Position() - 8);
		}
		m.raw.assign(a.Data().begin() + payload, a.Data().begin() + a.Position());
		p.misc.push_back(m);
	}
}


void
ReadWPPart(DocArchive& a, WPPart& out)
{
	a.CheckSig(Sig("PART"));
	a.UInt32();		// part kind
	ReadPartHeader(a, out.header);

	a.CheckSig(Sig("wp++"));
	if (a.MajorUserVersion() > 1) {
		out.version98 = a.UInt16();
		if (a.BuildVersion() > 0)
			out.version9a = a.UInt16();
	}
	out.value88 = a.UInt32();
	out.style = a.UInt32();
	out.value8c = a.UInt32();
	out.value94 = a.UInt32();

	out.bodyStart = a.Position();
	a.CheckSig(Sig("par+"));
	uint32_t count = a.UInt32();
	out.paragraphs.resize(count);
	for (WPParagraph& p : out.paragraphs)
		ReadParagraph(a, p);
	a.CheckSig(Sig("par-"));

	a.CheckSig(Sig("txt+"));
	for (WPParagraph& p : out.paragraphs) {
		std::vector<uint8_t> text = a.Bytes(p.length);
		p.text.assign(text.begin(), text.end());
	}
	a.CheckSig(Sig("txt-"));

	a.CheckSig(Sig("sct+"));
	uint32_t sections = a.UInt32();
	for (uint32_t i = 0; i < sections; i++) {
		WPSection s;
		s.start = a.UInt32();
		s.values[0] = a.UInt16();
		s.values[1] = a.UInt16();
		s.flag = a.Bool();
		uint32_t columns = a.UInt32();
		for (uint32_t c = 0; c < columns; c++) {
			std::array<float, 4> r;
			a.Rect(r.data());
			s.columns.push_back(r);
		}
		out.sections.push_back(std::move(s));
	}
	a.CheckSig(Sig("sct-"));
	a.CheckSig(Sig("wp--"));
	out.bodyEnd = a.Position();
}


uint32_t
MapTextOffset(const WPPart& part, uint32_t offset, bool atEnd)
{
	int64_t pos = offset;
	for (const auto& [at, delta] : part.edits) {
		if (delta > 0) {
			if (pos > int64_t(at) || (pos == int64_t(at) && atEnd && at > 0))
				pos += delta;
		} else {
			int64_t end = int64_t(at) - delta;
			if (pos >= end)
				pos += delta;
			else if (pos > int64_t(at))
				pos = at;
		}
	}
	return uint32_t(std::max<int64_t>(0, pos));
}


std::vector<uint8_t>
WriteWPPart(const WPPart& part, const std::vector<uint8_t>& record)
{
	DocWriter w;
	w.Bytes(record.data(), part.bodyStart);
	w.Sig(Sig("par+"));
	w.UInt32(uint32_t(part.paragraphs.size()));
	uint32_t start = 0;
	for (const WPParagraph& p : part.paragraphs) {	// TParagraph::Write
		w.UInt32(start);
		w.UInt32(uint32_t(p.text.size()));
		start += uint32_t(p.text.size());
		w.UInt16(p.value12);
		w.UInt32(p.parStyle);
		w.UInt32(uint32_t(p.runs.size()));
		for (const WPStyleRun& r : p.runs) {
			w.UInt32(r.position);
			w.UInt32(r.style);
		}
		w.UInt32(uint32_t(p.misc.size()));
		for (const WPMiscInfo& m : p.misc) {
			w.UInt32(m.kind);
			w.UInt32(m.position);
			w.Bytes(m.raw);
		}
	}
	w.Sig(Sig("par-"));
	w.Sig(Sig("txt+"));
	for (const WPParagraph& p : part.paragraphs)
		w.Bytes(reinterpret_cast<const uint8_t*>(p.text.data()), p.text.size());
	w.Sig(Sig("txt-"));
	w.Sig(Sig("sct+"));
	w.UInt32(uint32_t(part.sections.size()));
	for (const WPSection& s : part.sections) {
		w.UInt32(s.start);
		w.UInt16(s.values[0]);
		w.UInt16(s.values[1]);
		w.Bool(s.flag);
		w.UInt32(uint32_t(s.columns.size()));
		for (const auto& r : s.columns)
			w.Rect(r.data());
	}
	w.Sig(Sig("sct-"));
	w.Sig(Sig("wp--"));
	w.Bytes(record.data() + part.bodyEnd, record.size() - part.bodyEnd);
	return w.Data();
}

} // namespace gobe
