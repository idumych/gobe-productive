#include "ChartPart.h"

namespace gobe {

namespace {

std::vector<std::string>
ReadStrings(DocArchive& a)
{
	std::vector<std::string> list;
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++)
		list.push_back(a.String());
	return list;
}

} // namespace


void
ReadChartPart(DocArchive& a, ChartPart& out)
{
	ReadGRPart(a, out.graphics);
	a.CheckSig(Sig("cht+"));

	// TDataAccessor::TDataAccessor(TInputArchive&, TChartPart*)
	ChartData& d = out.data;
	a.CheckSig(Sig("daa+"));
	d.link = a.UInt32();
	d.values[0] = a.UInt32();
	d.values[1] = a.UInt32();
	if (a.FileFormatVersion() >= 0x15) {
		d.values[2] = a.UInt32();
		d.values[3] = a.UInt32();
	}
	d.values[4] = a.UInt32();
	d.names1 = ReadStrings(a);
	d.names2 = ReadStrings(a);
	d.value40 = a.UInt32();
	d.value44 = a.UInt32();
	d.flag4c = a.UInt32() != 0;
	a.CheckSig(Sig("tzt+"));		// TZTable::Read
	uint32_t rows = a.UInt32();
	for (uint32_t i = 0; i < rows; i++) {
		std::vector<float> row;
		uint32_t n = a.UInt32();
		for (uint32_t k = 0; k < n; k++)
			row.push_back(a.Float());
		d.table.push_back(std::move(row));
	}
	a.CheckSig(Sig("tzt-"));
	a.CheckSig(Sig("daa-"));
	out.chartType = a.UInt32();

	// TChart::Read (the chart subclasses read their axes etc. after it)
	a.CheckSig(Sig("cht+"));
	a.CheckSig(Sig("lgn+"));
	a.Rect(out.legendRect);
	uint32_t count = a.UInt32();
	for (uint32_t i = 0; i < count; i++) {
		ChartSeries series;
		a.CheckSig(Sig("ser+"));
		series.name = a.String();
		series.shown = a.Bool();
		series.style = a.UInt32();
		a.CheckSig(Sig("ser-"));
		out.series.push_back(std::move(series));
	}
	a.CheckSig(Sig("lgn-"));
}


float
ChartPart::Value(size_t s, size_t c) const
{
	size_t index = s * Categories() + c;
	size_t at = 0;
	for (const auto& row : data.table) {
		if (index < at + row.size())
			return row[index - at];
		at += row.size();
	}
	return 0;
}

} // namespace gobe
