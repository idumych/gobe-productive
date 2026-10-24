#include "DocSections.h"

#include "DocWriter.h"

#include <map>

#include "ResourceFile.h"

namespace gobe {

void
ReadVersionMgr(DocArchive& a, VersionMgr& out)
{
	a.CheckSig(Sig("VERM"));
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++)
		out.versions.push_back(ReadVersion(a));
	a.CheckSig(Sig("VerM"));
}


void
ReadPartFactoryTable(DocArchive& a, std::vector<PartFactoryEntry>& out)
{
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		PartFactoryEntry e;
		e.kind = a.UInt32();
		e.id = a.UInt32();
		e.name = a.String();
		out.push_back(e);
	}
}


void
ReadDocumentMisc(DocArchive& a, DocumentMisc& out)
{
	out.blockCount = a.UInt32();
	out.value70 = a.UInt32();
	for (int i = 0; i < 5; i++)
		out.messages.push_back(a.Message());
}


void
ReadPropertyManager(DocArchive& a, std::vector<Property>& out)
{
	a.CheckSig(Sig("PRPM"));
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		a.CheckSig(Sig("PROP"));
		Property p;
		p.type = a.UInt32();
		uint32_t size = a.UInt32();
		p.data = a.Bytes(size);
		a.CheckSig(Sig("ProP"));
		out.push_back(std::move(p));
	}
	a.CheckSig(Sig("PrpM"));
}


std::vector<uint32_t>
ReadPropsTable(DocArchive& a)
{
	std::vector<uint32_t> indexes;
	a.CheckSig(Sig("PTAB"));
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++)
		indexes.push_back(a.UInt32());
	a.CheckSig(Sig("PtaB"));
	return indexes;
}


void
ReadStyle(DocArchive& a, Style& out)
{
	a.CheckSig(Sig("STYL"));
	out.id = a.UInt32();
	out.value4 = a.UInt32();
	out.value8 = a.UInt32();
	out.valueC = a.UInt16();
	out.value10 = a.UInt32();
	out.name = a.String();
	for (uint32_t& link : out.links)
		link = a.UInt32();
	out.props = ReadPropsTable(a);
	out.props2 = ReadPropsTable(a);
	uint32_t subs = a.UInt32();
	for (uint32_t i = 0; i < subs; i++) {
		uint16_t key = a.UInt16();
		Style sub;
		ReadStyle(a, sub);
		out.substyles.emplace_back(key, std::move(sub));
	}
	a.CheckSig(Sig("STYL"));
}


void
ReadStyleMgr(DocArchive& a, StyleMgr& out)
{
	a.CheckSig(Sig("STYM"));
	ReadPropertyManager(a, out.properties);
	a.CheckSig(Sig("STYP"));
	uint32_t names = a.UInt32();
	for (uint32_t i = 0; i < names; i++) {
		uint32_t id = a.UInt32();
		out.names.emplace_back(id, a.String());
	}
	a.CheckSig(Sig("StyP"));
	a.CheckSig(Sig("STLT"));
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		Style style;
		ReadStyle(a, style);
		out.styles.push_back(std::move(style));
	}
	a.CheckSig(Sig("StlT"));
	a.CheckSig(Sig("StyM"));
	out.afterSection = a.UInt32();	// TDoc +0x6c
}


void
ReadPartMgr(DocArchive& a, PartMgr& out)
{
	a.CheckSig(Sig("PARM"));
	out.value14 = a.UInt32();
	uint32_t count = a.UInt32();
	std::vector<std::pair<uint32_t, uint32_t>> index;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t kind = a.UInt32();
		uint32_t size = a.UInt32();
		index.emplace_back(kind, size);
	}
	for (uint32_t i = 0; i < count; i++) {
		// the record size counts the size field itself
		uint32_t size = a.UInt32();
		PartRecord part;
		part.offset = a.Position();
		if (size < 16)
			throw ArchiveError("part record too small", part.offset);
		part.data = a.Bytes(size - 4);
		if ( part.data[0] != 'P' || part.data[1] != 'A'
				|| part.data[2] != 'R' || part.data[3] != 'T')
			throw ArchiveError("part record without 'PART'", part.offset);
		part.kind = uint32_t(part.data[4]) << 24 | uint32_t(part.data[5]) << 16
			| uint32_t(part.data[6]) << 8 | part.data[7];
		if (part.kind != index[i].first || size != index[i].second)
			throw ArchiveError("part record doesn't match the part index", part.offset);
		out.parts.push_back(std::move(part));
	}
	a.CheckSig(Sig("ParM"));
}


const Selection*
SelectionMgr::Find(uint32_t id) const
{
	for (const Selection& s : selections)
		if (s.id == id)
			return &s;
	return nullptr;
}


void
ReadSelectionMgr(DocArchive& a, SelectionMgr& out)
{
	a.CheckSig(Sig("SELM"));
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		Selection s;
		s.id = i;
		uint32_t size = a.UInt32();
		size_t end = a.Position() + size;
		s.kind = a.UInt32();
		auto base = [&]() {
			s.part = a.UInt32();
			s.value = a.UInt32();
		};
		switch (s.kind) {
			case Sig("SPRD"): {	// TSSSelection
				base();
				a.UInt32();
				s.cellAt = a.Position();
				s.cell[0] = a.UInt16();
				s.cell[1] = a.UInt16();
				uint32_t n = a.UInt32();
				for (uint32_t k = 0; k < n; k++) {
					std::array<uint16_t, 4> r;
					for (uint16_t& v : r)
						v = a.UInt16();
					s.ranges.push_back(r);
				}
				a.Bool();
				break;
			}
			case Sig("WORD"): {	// TWPSelection
				base();
				uint32_t n = a.UInt32();
				s.textRangesAt = a.Position();
				for (uint32_t k = 0; k < n; k++) {
					uint32_t from = a.UInt32();
					s.textRanges.emplace_back(from, a.UInt32());
				}
				a.Bool();
				break;
			}
			case Sig("GRFX"): {	// TGRSelection: TShapeIDs
				base();
				uint32_t n = a.UInt32();
				for (uint32_t k = 0; k < n; k++) {
					a.UInt32();
					a.UInt32();
				}
				break;
			}
			case Sig("CLOK"):
				base();
				break;
			case Sig("BITS"): {	// TIMSelection
				base();
				float r[4];
				if (a.FileFormatVersion() < 0x16)
					a.Rect(r);
				else if (a.Bool()) {
					a.Rect(r);
					a.Rect(r);
				}
				break;
			}
			default:
				break;
		}
		// the record size covers what the part's ReadSelection reads
		// (bitmaps, version-dependent flags)
		if (a.Position() > end)
			throw ArchiveError("selection longer than its record", a.Position());
		a.Seek(end);
		out.selections.push_back(std::move(s));
	}
	a.CheckSig(Sig("SelM"));
}


void
ReadLinksMgr(DocArchive& a, LinksMgr& out)
{
	a.CheckSig(Sig("LINK"));
	out.value0 = a.Bool();
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		Link l;
		l.value20 = a.UInt32();
		l.flag1 = a.Bool();
		l.sourcePart = a.UInt32();
		l.dependentPart = a.UInt32();
		l.name = a.String();
		l.value1c = a.UInt32();
		l.reference = a.UInt32();
		out.links.push_back(std::move(l));
	}
	a.CheckSig(Sig("LinK"));
}


std::vector<uint8_t>
WritePropertyManager(const std::vector<Property>& properties)
{
	DocWriter w;
	w.Sig(Sig("PRPM"));
	w.UInt32(uint32_t(properties.size()));
	for (const Property& p : properties) {
		w.Sig(Sig("PROP"));
		w.UInt32(p.type);
		w.UInt32(uint32_t(p.data.size()));
		w.Bytes(p.data);
		w.Sig(Sig("ProP"));
	}
	w.Sig(Sig("PrpM"));
	return w.Data();
}


std::vector<uint8_t>
WritePropsTable(const std::vector<uint32_t>& indexes)
{
	DocWriter w;
	w.Sig(Sig("PTAB"));
	w.UInt32(uint32_t(indexes.size()));
	for (uint32_t idx : indexes)
		w.UInt32(idx);
	w.Sig(Sig("PtaB"));
	return w.Data();
}


std::vector<uint8_t>
WriteStyle(const Style& style)
{
	DocWriter w;
	w.Sig(Sig("STYL"));
	w.UInt32(style.id);
	w.UInt32(style.value4);
	w.UInt32(style.value8);
	w.UInt16(style.valueC);
	w.UInt32(style.value10);
	w.String(style.name);
	for (uint32_t link : style.links)
		w.UInt32(link);
	w.Bytes(WritePropsTable(style.props));
	w.Bytes(WritePropsTable(style.props2));
	w.UInt32(uint32_t(style.substyles.size()));
	for (const auto& sub : style.substyles) {
		w.UInt16(sub.first);
		w.Bytes(WriteStyle(sub.second));
	}
	w.Sig(Sig("STYL"));
	return w.Data();
}


std::vector<uint8_t>
WriteStyleMgr(const StyleMgr& styles)
{
	DocWriter w;
	w.Sig(Sig("STYM"));
	w.Bytes(WritePropertyManager(styles.properties));
	w.Sig(Sig("STYP"));
	w.UInt32(uint32_t(styles.names.size()));
	for (const auto& [id, name] : styles.names) {
		w.UInt32(id);
		w.String(name);
	}
	w.Sig(Sig("StyP"));
	w.Sig(Sig("STLT"));
	w.UInt32(uint32_t(styles.styles.size()));
	for (const Style& style : styles.styles) {
		w.Bytes(WriteStyle(style));
	}
	w.Sig(Sig("StlT"));
	w.Sig(Sig("StyM"));
	w.UInt32(styles.afterSection);
	return w.Data();
}


std::vector<uint8_t>
WriteLinksMgr(const LinksMgr& links)
{
	DocWriter w;
	w.Sig(Sig("LINK"));
	w.Bool(links.value0);
	w.UInt32(uint32_t(links.links.size()));
	for (const Link& l : links.links) {	// TLink::Write
		w.UInt32(l.value20);
		w.Bool(l.flag1);
		w.UInt32(l.sourcePart);
		w.UInt32(l.dependentPart);
		w.String(l.name);
		w.UInt32(l.value1c);
		w.UInt32(l.reference);
	}
	w.Sig(Sig("LinK"));
	return w.Data();
}


std::vector<uint8_t>
WriteSetMgr()
{
	DocWriter w;
	w.Sig(Sig("SETM"));
	w.UInt32(0);	// ink set count 0
	w.UInt32(0);
	w.UInt32(0);
	w.Sig(Sig("SetM"));
	return w.Data();
}


std::vector<uint8_t>
WritePenMgr()
{
	DocWriter w;
	w.Sig(Sig("PENM"));
	w.UInt8(1);	// pen flag
	w.UInt8(0);	// bool
	w.UInt32(0);	// pen count
	// double-passthrough: same data repeated (no pens, just the signature)
	w.Sig(Sig("PenM"));
	return w.Data();
}


void
ReadSheetMgr(DocArchive& a, SheetMgr& out)
{
	out.name = a.String();
	out.value4 = a.UInt32();
	out.value8 = a.UInt32();
	out.sheetCount = a.UInt32();
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		LeafSheet s;
		s.name = a.String();
		s.value4 = a.UInt32();
		s.value8 = a.UInt32();
		s.mainPart = a.UInt32();
		s.graphicsPart = a.UInt32();
		s.headerPart = a.UInt32();
		s.footerPart = a.UInt32();
		s.value24 = a.UInt32();
		s.value28 = a.UInt32();
		s.frameSheet = a.Bool();
		s.value2d = a.Bool();
		a.Rect(s.paper);
		a.Rect(s.printable);
		for (bool& f : s.flags)
			f = a.Bool();
		s.unit = a.UInt32();
		s.divisions = a.UInt32();
		uint32_t size = a.UInt32();
		s.printSettings = a.Bytes(size);
		out.sheets.push_back(std::move(s));
	}
}


std::vector<uint8_t>
WriteSheetMgr(const SheetMgr& mgr)
{
	DocWriter w;
	w.String(mgr.name);
	w.UInt32(mgr.value4);
	w.UInt32(mgr.value8);
	w.UInt32(mgr.sheetCount);
	w.UInt32(uint32_t(mgr.sheets.size()));
	for (const LeafSheet& s : mgr.sheets) {
		w.String(s.name);
		w.UInt32(s.value4);
		w.UInt32(s.value8);
		w.UInt32(s.mainPart);
		w.UInt32(s.graphicsPart);
		w.UInt32(s.headerPart);
		w.UInt32(s.footerPart);
		w.UInt32(s.value24);
		w.UInt32(s.value28);
		w.Bool(s.frameSheet);
		w.Bool(s.value2d);
		w.Rect(s.paper);
		w.Rect(s.printable);
		for (bool f : s.flags)
			w.Bool(f);
		w.UInt32(s.unit);
		w.UInt32(s.divisions);
		w.UInt32(uint32_t(s.printSettings.size()));
		w.Bytes(s.printSettings);
	}
	return w.Data();
}


std::vector<uint8_t>
WriteCommandMgr()
{
	DocWriter w;
	w.Sig(Sig("CMDM"));
	w.UInt32(0);	// command count
	w.Sig(Sig("CmdM"));
	return w.Data();
}


std::vector<uint8_t>
WriteHistoryMgr()
{
	// TOrderTable::Write: count, then two u32s
	DocWriter w;
	w.UInt32(0);	// count
	w.UInt32(0);	// two u32s
	w.UInt32(0);
	return w.Data();
}


std::vector<uint8_t>
WriteWindowMgr()
{
	// TDocWindow::Write per window: doc pointer, internal TOC, window name
	// For blank: 0 windows
	DocWriter w;
	w.UInt32(0);	// window count
	return w.Data();
}


std::vector<uint8_t>
WriteFactoryTable(const std::vector<PartFactoryEntry>& factories)
{
	DocWriter w;
	w.UInt32(uint32_t(factories.size()));
	for (const auto& f : factories) {
		w.UInt32(f.kind);
		w.UInt32(f.id);
		w.String(f.name);
	}
	return w.Data();
}


std::vector<uint8_t>
WriteDocumentMisc(const DocumentMisc& misc)
{
	DocWriter w;
	w.UInt32(misc.blockCount);
	w.UInt32(misc.value70);
	for (const auto& msg : misc.messages)
		w.Bytes(msg);
	return w.Data();
}


std::vector<uint8_t>
WriteSelectionMgr(const SelectionMgr& mgr)
{
	DocWriter w;
	w.Sig(Sig("SELM"));
	w.UInt32(uint32_t(mgr.selections.size()));
	for (const auto& sel : mgr.selections) {
		w.UInt32(0);	// record size placeholder
		w.UInt32(sel.kind);
		w.UInt32(sel.part);
		w.UInt32(sel.value);
		// ... selection-specific data ...
	}
	w.Sig(Sig("SelM"));
	return w.Data();
}


std::vector<uint8_t>
WritePartMgrFromRecords(const PartMgr& mgr)
{
	// Writes the PartMgr section from PartRecord data.
	// Format: 'PARM', value14, count, (kind, size) per part,
	// (size, record) per part, 'ParM'
	DocWriter w;
	w.Sig(Sig("PARM"));
	w.UInt32(mgr.value14);
	w.UInt32(uint32_t(mgr.parts.size()));
	for (const auto& p : mgr.parts) {
		w.UInt32(p.kind);
		w.UInt32(uint32_t(p.data.size() + 4));	// size includes the size field itself
	}
	for (const auto& p : mgr.parts) {
		w.UInt32(uint32_t(p.data.size() + 4));
		w.Bytes(p.data);
	}
	w.Sig(Sig("ParM"));
	return w.Data();
}


bool
ReadSection(DocArchive& a, const std::string& name, std::string& summary)
{
	if (name == "VersionMgr") {
		VersionMgr v;
		ReadVersionMgr(a, v);
		summary = std::to_string(v.versions.size()) + " versions";
		return true;
	}
	if (name == "PartFactory Table") {
		std::vector<PartFactoryEntry> t;
		ReadPartFactoryTable(a, t);
		for (auto& e : t)
			summary += FourCCString(e.kind) + "=" + e.name + " ";
		return true;
	}
	if (name == "Document Misc") {
		DocumentMisc m;
		ReadDocumentMisc(a, m);
		int n = 0;
		for (auto& msg : m.messages)
			n += !msg.empty();
		summary = std::to_string(n) + " messages";
		return true;
	}
	if (name == "StyleMgr") {
		StyleMgr m;
		ReadStyleMgr(a, m);
		summary = std::to_string(m.properties.size()) + " properties, "
			+ std::to_string(m.styles.size()) + " styles:";
		for (size_t i = 0; i < m.styles.size() && i < 6; i++)
			summary += " [" + m.styles[i].name + "]";
		return true;
	}
	if (name == "SelectionMgr") {
		SelectionMgr m;
		ReadSelectionMgr(a, m);
		std::map<std::string, int> kinds;
		for (auto& s : m.selections)
			kinds[FourCCString(s.kind)]++;
		summary = std::to_string(m.selections.size()) + " selections:";
		for (auto& [k, n] : kinds)
			summary += " " + k + "=" + std::to_string(n);
		return true;
	}
	if (name == "LinksMgr") {
		LinksMgr m;
		ReadLinksMgr(a, m);
		summary = std::to_string(m.links.size()) + " links:";
		for (auto& l : m.links)
			summary += " [" + l.name + "]";
		return true;
	}
	if (name == "PartMgr") {
		PartMgr m;
		ReadPartMgr(a, m);
		std::map<std::string, int> kinds;
		for (auto& p : m.parts)
			kinds[FourCCString(p.kind)]++;
		summary = std::to_string(m.parts.size()) + " parts:";
		for (auto& [k, n] : kinds)
			summary += " " + k + "x" + std::to_string(n);
		return true;
	}
	return false;
}

} // namespace gobe
