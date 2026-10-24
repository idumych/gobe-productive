#include "SSPart.h"

#include <algorithm>

#include "DocWriter.h"

namespace gobe {

namespace {

SSRange
ReadRange(DocArchive& a)
{
	SSRange r;
	r.from = ReadCellID(a);
	r.to = ReadCellID(a);
	return r;
}


// TCellsList::Read
std::vector<SSRange>
ReadCellsList(DocArchive& a)
{
	std::vector<SSRange> list;
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++)
		list.push_back(ReadRange(a));
	return list;
}


// TDataCell::TDataCell(TInputArchive&)
void
ReadDataCell(DocArchive& a, SSCell& c)
{
	c.flags = a.UInt16();
	c.style = a.UInt32();
}


// TSSDataManager::ReadCell
SSCell
ReadCell(DocArchive& a)
{
	SSCell c;
	c.kind = a.UInt32();
	switch (c.kind) {
		case Sig("NONE"):
			ReadDataCell(a, c);
			break;
		case Sig("Head"):
			ReadDataCell(a, c);
			c.number = a.Float();
			break;
		case Sig("Bool"):
			ReadDataCell(a, c);
			c.number = a.Bool() ? 1 : 0;
			break;
		case Sig("Err#"):
			ReadDataCell(a, c);
			c.value = a.UInt32();
			break;
		case Sig("Lng#"):	// TNumberCell adds nothing to the archive
			ReadDataCell(a, c);
			c.value = a.UInt32();
			c.number = int32_t(c.value);
			break;
		case Sig("Time"):
			ReadDataCell(a, c);
			c.value = a.UInt32();
			c.number = c.value;
			break;
		case Sig("Flt#"):
			ReadDataCell(a, c);
			c.number = a.Float();
			break;
		case Sig("Dbl#"):
			ReadDataCell(a, c);
			c.number = a.Double();
			break;
		case Sig("Strn"):
			ReadDataCell(a, c);
			c.text = a.String();
			break;
		case Sig("Fmla"):
			ReadDataCell(a, c);
			c.formula = ReadFormula(a);
			break;
		case Sig("FmEr"):	// TBadFormulaCell: a TErrorCell + two strings
			ReadDataCell(a, c);
			c.value = a.UInt32();
			c.text = a.String();
			c.text2 = a.String();
			c.extra[0] = a.UInt32();
			c.extra[1] = a.UInt32();
			break;
		default:
			// the original substitutes the null cell without reading more,
			// which loses the stream position
			throw ArchiveError("unknown cell kind", a.Position() - 4);
	}
	return c;
}


SSCell
ReadHeaderCell(DocArchive& a)
{
	SSCell c;
	c.kind = Sig("Head");
	ReadDataCell(a, c);
	c.number = a.Float();
	return c;
}

} // namespace


const SSCell*
SSPart::Cell(uint16_t ca, uint16_t cb) const
{
	for (const SSCell& c : cells)
		if (c.id.a == ca && c.id.b == cb)
			return &c;
	return nullptr;
}


const SSHeader*
SSPart::Column(uint16_t column) const
{
	for (const SSHeader& h : columns)
		if (h.index == column)
			return &h;
	return nullptr;
}


const SSHeader*
SSPart::Row(uint16_t row) const
{
	for (const SSHeader& h : rows)
		if (h.index == row)
			return &h;
	return nullptr;
}


float
SSPart::ColumnWidth(uint16_t column) const
{
	const SSHeader* h = Column(column);
	return h ? float(h->cell.number) : defaultWidth;
}


float
SSPart::RowHeight(uint16_t row) const
{
	const SSHeader* h = Row(row);
	return h ? float(h->cell.number) : defaultHeight;
}


void
ReadSSPart(DocArchive& a, SSPart& out)
{
	a.CheckSig(Sig("PART"));
	a.UInt32();
	ReadPartHeader(a, out.header);

	out.headerStart = a.Position();
	a.CheckSig(Sig("ss +"));
	out.used = ReadRange(a);
	out.style = a.UInt32();
	out.locked = ReadCellsList(a);
	out.autoCalculate = a.Bool();

	// TSSDataManager::Read
	a.CheckSig(Sig("coli"));
	while (a.UInt32() != 0) {
		SSHeader h;
		h.index = a.UInt32();
		h.cell = ReadHeaderCell(a);
		out.columns.push_back(std::move(h));
	}
	a.CheckSig(Sig("rowi"));
	while (a.UInt32() != 0) {
		SSHeader h;
		h.index = a.UInt32();
		h.cell = ReadHeaderCell(a);
		out.rows.push_back(std::move(h));
	}
	out.dataStart = a.Position();
	a.CheckSig(Sig("data"));
	for (;;) {
		CellID id = ReadCellID(a);
		if (id.b == 0xffff) {
			out.dataEndID = id;
			out.dataEnd = a.Position();
			break;
		}
		SSCell c = ReadCell(a);
		c.id = id;
		out.cells.push_back(std::move(c));
	}

	// TSSDepManager(TPartSS*, TInputArchive&)
	out.depmStart = a.Position();
	a.CheckSig(Sig("depm"));
	uint32_t count = a.UInt32();		// TOrderTable<TCellID>
	for (uint32_t i = 0; i < count; i++)
		out.dependents.push_back(ReadCellID(a));
	out.linkDeps = a.UInt32();			// TOrderTable<TLinkDepInfo>
	for (uint32_t i = 0; i < out.linkDeps; i++) {
		SSPart::LinkDep d;
		d.link = a.UInt32();				// TLink*
		d.cells = ReadCellsList(a);
		d.flag = a.Bool();
		d.value = a.UInt32();
		out.linkDepInfo.push_back(std::move(d));
	}
	out.depmEnd = a.Position();

	// TSSDisplayManager(TPartSS*, TInputArchive&)
	a.CheckSig(Sig("disp"));
	out.defaultHeight = a.Float();
	out.defaultWidth = a.Float();
	for (uint32_t& s : out.displayStyles)
		s = a.UInt32();
	a.CheckSig(Sig("dopt"));
	for (bool& o : out.options)
		o = a.Bool();
	out.optionsEnd = a.Position();
	count = a.UInt32();					// TSSFillManager: TOrderTable<TSSFillInfo>
	for (uint32_t i = 0; i < count; i++) {
		SSFill f;
		f.cells = ReadCellsList(a);
		f.style = a.UInt32();
		f.range = ReadRange(a);
		a.Rect(f.rect);
		out.fills.push_back(std::move(f));
	}
	if (a.FileFormatVersion() > 0x15)
		for (uint32_t& v : out.frame)
			v = a.UInt32();

	a.CheckSig(Sig("ss -"));
}



std::vector<uint8_t>
WriteSSPart(const SSPart& part, const std::vector<uint8_t>& record)
{
	DocWriter w;
	w.Bytes(record.data(), part.headerStart);
	// TPartSS::Write
	auto range = [&](const SSRange& r) {
		w.UInt16(r.from.a);
		w.UInt16(r.from.b);
		w.UInt16(r.to.a);
		w.UInt16(r.to.b);
	};
	w.Sig(Sig("ss +"));
	range(part.used);
	w.UInt32(part.style);
	w.UInt32(uint32_t(part.locked.size()));		// TCellsList::Write
	for (const SSRange& r : part.locked)
		range(r);
	w.Bool(part.autoCalculate);
	// TSSDataManager::Write: the column and row headers in order, each
	// after its kind ('Head'), then a 0
	for (int which = 0; which < 2; which++) {
		w.Sig(Sig(which == 0 ? "coli" : "rowi"));
		std::vector<SSHeader> headers = which == 0 ? part.columns : part.rows;
		std::stable_sort(headers.begin(), headers.end(),
			[](const SSHeader& x, const SSHeader& y) { return x.index < y.index; });
		for (const SSHeader& h : headers) {
			w.UInt32(h.cell.kind);
			w.UInt32(h.index);
			w.UInt16(h.cell.flags);
			w.UInt32(h.cell.style);
			w.Float(float(h.cell.number));
		}
		w.UInt32(0);
	}
	w.Sig(Sig("data"));
	for (const SSCell& c : part.cells) {	// TSSDataManager::Write
		w.UInt16(c.id.a);
		w.UInt16(c.id.b);
		w.UInt32(c.kind);
		w.UInt16(c.flags);			// TDataCell::Write
		w.UInt32(c.style);
		switch (c.kind) {
			case Sig("Head"):
			case Sig("Flt#"):
				w.Float(float(c.number));
				break;
			case Sig("Bool"):
				w.Bool(c.number != 0);
				break;
			case Sig("Err#"):
			case Sig("Lng#"):
			case Sig("Time"):
				w.UInt32(c.value);
				break;
			case Sig("Dbl#"):
				w.Double(c.number);
				break;
			case Sig("Strn"):
				w.String(c.text);
				break;
			case Sig("Fmla"):
				WriteFormula(w, c.formula);
				break;
			case Sig("FmEr"):
				w.UInt32(c.value);
				w.String(c.text);
				w.String(c.text2);
				w.UInt32(c.extra[0]);
				w.UInt32(c.extra[1]);
				break;
			default:
				break;
		}
	}
	w.UInt16(part.dataEndID.a);
	w.UInt16(part.dataEndID.b);
	// what lies between (nothing in the samples) is copied
	w.Bytes(record.data() + part.dataEnd, part.depmStart - part.dataEnd);
	// TSSDepManager::Write
	w.Sig(Sig("depm"));
	w.UInt32(uint32_t(part.dependents.size()));
	for (const CellID& id : part.dependents) {
		w.UInt16(id.a);
		w.UInt16(id.b);
	}
	w.UInt32(uint32_t(part.linkDepInfo.size()));
	for (const SSPart::LinkDep& d : part.linkDepInfo) {
		w.UInt32(d.link);
		w.UInt32(uint32_t(d.cells.size()));
		for (const SSRange& r : d.cells) {
			w.UInt16(r.from.a);
			w.UInt16(r.from.b);
			w.UInt16(r.to.a);
			w.UInt16(r.to.b);
		}
		w.Bool(d.flag);
		w.UInt32(d.value);
	}
	// TSSDisplayManager::Write: row height, column width, its styles and
	// the display options (TSSDisplayOptions)
	w.Sig(Sig("disp"));
	w.Float(part.defaultHeight);
	w.Float(part.defaultWidth);
	for (uint32_t s : part.displayStyles)
		w.UInt32(s);
	w.Sig(Sig("dopt"));
	for (bool o : part.options)
		w.Bool(o);
	w.Bytes(record.data() + part.optionsEnd, record.size() - part.optionsEnd);
	return w.Data();
}

} // namespace gobe
