#include "TablePart.h"

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
}

} // namespace gobe
