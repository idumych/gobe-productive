// chart_write: TChartPart::Write's chart section (WriteChartSection) of
// every chart in the documents, compared byte for byte with what it was
// read from; the 2DSM charts' axes and series sets as read.
// Arguments: documents (CheckBook and SlideShow have 2DSM charts,
// TheWidget a BAR3).
#include <cstdio>
#include <string>

#include "ChartPart.h"
#include "Document.h"

using namespace gobe;

int
main(int argc, char** argv)
{
	int ok = 0, differ = 0, axisCharts = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		int format = doc.file.header.version.values[2];
		for (const auto& [index, chart] : doc.chartParts) {
			const std::vector<uint8_t>& record = doc.parts.parts[index].data;
			std::vector<uint8_t> written = WriteChartSection(chart, format);
			bool same = chart.sectionEnd > chart.sectionStart && chart.sectionEnd <= record.size()
				&& chart.sectionStart == chart.graphics.dataEnd
				&& written.size() == chart.sectionEnd - chart.sectionStart
				&& std::equal(written.begin(), written.end(),
					record.begin() + long(chart.sectionStart));
			if (chart.chartType == Sig("2DSM")) {
				axisCharts++;
				// a 2DSM chart: two axes and its sets read (none left raw)
				same = same && chart.chartBody.empty() && !chart.sets.empty();
				size_t series = 0;
				for (const ChartSeriesSet& set : chart.sets)
					series += set.series.size();
				same = same && series == chart.SeriesCount();
			}
			if (same)
				ok++;
			else {
				differ++;
				printf("%s: chart part %zu differs (%zu bytes written, %zu read)\n", argv[i], index,
					written.size(), chart.sectionEnd - chart.sectionStart);
			}
		}
	}
	printf("chart_write: charts %d (2DSM %d), differ %d\n", ok, axisCharts, differ);
	return differ || axisCharts == 0 ? 1 : 0;
}
