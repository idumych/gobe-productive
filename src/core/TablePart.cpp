#include "TablePart.h"

#include <algorithm>

#include "DocWriter.h"

namespace gobe {

void
ReadTablePart(DocArchive& a, TablePart& out)
{
	a.CheckSig(Sig("PART"));
	a.UInt32();
	ReadPartHeader(a, out.header);
	if (a.FileFormatVersion() < 0x18)
		throw ArchiveError("table from an older format", a.Position());

	// TTable::TTable(TInputArchive&, TTablePart*)
	out.bodyStart = a.Position();
	out.format = a.FileFormatVersion();
	a.Point(out.size);
	if (a.FileFormatVersion() >= 0x1d)
		out.value20 = a.Float();
	uint32_t rows = a.UInt32();
	for (uint32_t r = 0; r < rows; r++) {
		TableRow row;
		row.top = a.Float();
		row.height = a.Float();
		uint32_t cells = a.UInt32();
		for (uint32_t c = 0; c < cells; c++) {
			TableCell cell;
			cell.row = a.UInt32();
			cell.column = a.UInt32();
			a.Point(cell.origin);
			a.Point(cell.size);
			cell.values[0] = a.UInt32();
			cell.values[1] = a.UInt32();
			int format = a.FileFormatVersion();
			if (format == 0x19)
				a.UInt32();		// a TStyleID, no longer used
			else if (format > 0x19 && format < 0x1d)
				a.Float();
			for (uint32_t& v : cell.frame)
				v = a.UInt32();
			row.cells.push_back(cell);
		}
		out.rows.push_back(std::move(row));
	}
	out.textPart = a.UInt32();
	out.textPortal = a.UInt32();
	out.bodyEnd = a.Position();
}


std::vector<uint8_t>
WriteTableRecord(const TablePart& table, const std::vector<uint8_t>& record)
{
	DocWriter w;
	w.Bytes(record.data(), std::min(table.bodyStart, record.size()));
	w.Point(table.size);
	if (table.format >= 0x1d)
		w.Float(table.value20);
	w.UInt32(uint32_t(table.rows.size()));
	for (const TableRow& row : table.rows) {
		w.Float(row.top);
		w.Float(row.height);
		w.UInt32(uint32_t(row.cells.size()));
		for (const TableCell& cell : row.cells) {
			w.UInt32(cell.row);
			w.UInt32(cell.column);
			w.Point(cell.origin);
			w.Point(cell.size);
			w.UInt32(cell.values[0]);
			w.UInt32(cell.values[1]);
			if (table.format == 0x19)
				w.UInt32(0);
			else if (table.format > 0x19 && table.format < 0x1d)
				w.Float(0);
			for (uint32_t v : cell.frame)
				w.UInt32(v);
		}
	}
	w.UInt32(table.textPart);
	w.UInt32(table.textPortal);
	std::vector<uint8_t> out = w.Data();
	if (table.bodyEnd <= record.size())
		out.insert(out.end(), record.begin() + long(table.bodyEnd), record.end());
	return out;
}

} // namespace gobe
