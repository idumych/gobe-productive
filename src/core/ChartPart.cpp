#include "ChartPart.h"

#include "DocWriter.h"

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
	out.sectionStart = a.Position();
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
	out.chartValue8 = a.UInt32();
	// TOrderTable<TSurfaceLabel>: none in the documents seen; a table with
	// labels is kept from its count to the u32 and 'cht-' that end TChart
	size_t labelsAt = a.Position();
	uint32_t labels = a.UInt32();
	const std::vector<uint8_t>& bytes = a.Data();
	auto sigAt = [&](size_t at, uint32_t sig) {
		return at + 4 <= bytes.size() && (uint32_t(bytes[at]) << 24 | bytes[at + 1] << 16 | bytes[at + 2] << 8
			| bytes[at + 3]) == sig;
	};
	if (labels != 0) {
		size_t at = a.Position();
		while (at + 8 <= bytes.size() && !sigAt(at + 4, Sig("cht-")))
			at++;
		a.Seek(at);
	}
	out.surfaceLabels.assign(bytes.begin() + long(labelsAt), bytes.begin() + long(a.Position()));
	out.chartFlags = a.UInt32();
	a.CheckSig(Sig("cht-"));

	// the part's closing 'cht-' (the part tail, 'REFT', follows it)
	size_t end = a.Position();
	while (end + 8 <= bytes.size() && !(sigAt(end, Sig("cht-")) && sigAt(end + 4, Sig("REFT"))))
		end++;
	if (end + 8 > bytes.size())
		throw ArchiveError("chart part not closed", a.Position());
	size_t values = a.FileFormatVersion() >= 0x11 ? 8 : 0;
	if (out.chartType == Sig("2DSM")) {
		// T2DXYAxisChart::Write: the axes (TAxis::Write), the series kind,
		// the sets (each its kind and T2DXYSeriesSet::Write)
		for (ChartAxis& axis : out.axes) {
			a.CheckSig(Sig("axi+"));
			for (uint32_t& v : axis.values)
				v = a.UInt32();
			axis.style = a.UInt32();
			axis.flags[0] = a.Bool();
			axis.flags[1] = a.Bool();
			a.CheckSig(Sig("axi-"));
		}
		out.seriesKind = a.UInt32();
		uint32_t sets = a.UInt32();
		for (uint32_t i = 0; i < sets; i++) {
			ChartSeriesSet set;
			set.kind = a.UInt32();
			uint32_t n = a.UInt32();
			for (uint32_t k = 0; k < n; k++)
				set.series.push_back(a.UInt32());
			out.sets.push_back(set);
		}
		if (a.Position() + values != end)
			throw ArchiveError("2DSM chart's end", a.Position());
	} else {
		if (end < a.Position() + values)
			throw ArchiveError("chart body", a.Position());
		out.chartBody.assign(bytes.begin() + long(a.Position()), bytes.begin() + long(end - values));
		a.Seek(end - values);
	}
	if (values != 0) {
		out.partValues[0] = a.UInt32();
		out.partValues[1] = a.UInt32();
	}
	a.CheckSig(Sig("cht-"));
	out.sectionEnd = a.Position();
}


std::vector<uint8_t>
WriteChartSection(const ChartPart& part, int fileFormat)
{
	DocWriter w;
	w.Sig(Sig("cht+"));
	// TDataAccessor::Write
	const ChartData& d = part.data;
	w.Sig(Sig("daa+"));
	w.UInt32(d.link);
	w.UInt32(d.values[0]);
	w.UInt32(d.values[1]);
	if (fileFormat >= 0x15) {
		w.UInt32(d.values[2]);
		w.UInt32(d.values[3]);
	}
	w.UInt32(d.values[4]);
	for (const std::vector<std::string>* names : {&d.names1, &d.names2}) {
		w.UInt32(uint32_t(names->size()));
		for (const std::string& name : *names)
			w.String(name);
	}
	w.UInt32(d.value40);
	w.UInt32(d.value44);
	w.UInt32(d.flag4c ? 1 : 0);
	w.Sig(Sig("tzt+"));
	w.UInt32(uint32_t(d.table.size()));
	for (const std::vector<float>& row : d.table) {
		w.UInt32(uint32_t(row.size()));
		for (float v : row)
			w.Float(v);
	}
	w.Sig(Sig("tzt-"));
	w.Sig(Sig("daa-"));
	w.UInt32(part.chartType);
	// TChart::Write
	w.Sig(Sig("cht+"));
	w.Sig(Sig("lgn+"));
	w.Rect(part.legendRect);
	w.UInt32(uint32_t(part.series.size()));
	for (const ChartSeries& series : part.series) {
		w.Sig(Sig("ser+"));
		w.String(series.name);
		w.Bool(series.shown);
		w.UInt32(series.style);
		w.Sig(Sig("ser-"));
	}
	w.Sig(Sig("lgn-"));
	w.UInt32(part.chartValue8);
	if (part.surfaceLabels.empty())
		w.UInt32(0);
	else
		w.Bytes(part.surfaceLabels);
	w.UInt32(part.chartFlags);
	w.Sig(Sig("cht-"));
	if (part.chartType == Sig("2DSM")) {
		for (const ChartAxis& axis : part.axes) {
			w.Sig(Sig("axi+"));
			for (uint32_t v : axis.values)
				w.UInt32(v);
			w.UInt32(axis.style);
			w.Bool(axis.flags[0]);
			w.Bool(axis.flags[1]);
			w.Sig(Sig("axi-"));
		}
		w.UInt32(part.seriesKind);
		w.UInt32(uint32_t(part.sets.size()));
		for (const ChartSeriesSet& set : part.sets) {
			w.UInt32(set.kind);
			w.UInt32(uint32_t(set.series.size()));
			for (uint32_t s : set.series)
				w.UInt32(s);
		}
	} else
		w.Bytes(part.chartBody);
	if (fileFormat >= 0x11) {
		w.UInt32(part.partValues[0]);
		w.UInt32(part.partValues[1]);
	}
	w.Sig(Sig("cht-"));
	return w.Data();
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
