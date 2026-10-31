// gr_write: TPartGR::Write (WriteGRData) of every drawing, presentation
// and chart part in the documents, compared byte for byte with the data
// they were read from ('GRAP' to 'GraP' in the part record).
// Arguments: documents.
#include <cstdio>
#include <string>

#include "Document.h"
#include "GRPart.h"

using namespace gobe;

int
main(int argc, char** argv)
{
	int ok = 0, differ = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		int format = doc.file.header.version.values[2];
		int major = doc.file.header.version.values[0];
		auto check = [&](size_t index, const GRPart& gr, const char* what) {
			const std::vector<uint8_t>& record = doc.parts.parts[index].data;
			std::vector<uint8_t> written = WriteGRData(gr, format, major);
			bool same = gr.dataEnd <= record.size() && gr.dataEnd > gr.dataStart
				&& written.size() == gr.dataEnd - gr.dataStart
				&& std::equal(written.begin(), written.end(), record.begin() + long(gr.dataStart));
			if (same)
				ok++;
			else {
				differ++;
				printf("%s: %s part %zu differs (%zu bytes written, %zu read)\n", argv[i], what,
					index, written.size(), gr.dataEnd - gr.dataStart);
			}
		};
		for (const auto& [index, gr] : doc.graphicsParts)
			check(index, gr, "graphics");
		for (const auto& [index, chart] : doc.chartParts)
			check(index, chart.graphics, "chart");
	}
	printf("gr_write: parts %d, differ %d\n", ok, differ);
	return differ ? 1 : 0;
}
