// fill_patterns: bitmap pattern fills ('FPAT', TFillPattern). Every one
// in the original's ink set files and in the documents' style properties
// decodes (FillPatternPixels) to the size its bitmap header gives; the
// first Clouds swatch's pixels match the bytes inflated here (B, G, R
// in the file: R, G, B out, opaque).
// Arguments: the documents (the ink sets are found beside them).
#include <cstdio>
#include <cstring>
#include <string>

#include <zlib.h>

#include "DocArchive.h"
#include "Document.h"
#include "InkSets.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static uint32_t
BE32(const std::vector<uint8_t>& d, size_t at)
{
	return uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
}


static float
BEFloat(const std::vector<uint8_t>& d, size_t at)
{
	uint32_t bits = BE32(d, at);
	float f;
	memcpy(&f, &bits, 4);
	return f;
}


// the bitmap's size from its header: 'FPAT', size, 'TPAT', a byte,
// 'BMAP' (13), its size (17), a bool, the color space, bytes per row,
// the bounds (30)
static void
HeaderSize(const Ink& ink, int& width, int& height)
{
	const std::vector<uint8_t>& d = ink.otherFill;
	width = int(BEFloat(d, 38) - BEFloat(d, 30) + 1);
	height = int(BEFloat(d, 42) - BEFloat(d, 34) + 1);
}


static int sFills = 0;

static void
CheckFill(const Ink& ink, const std::string& where)
{
	if (ink.otherFill.size() < 46 || BE32(ink.otherFill, 0) != Sig("FPAT"))
		return;
	sFills++;
	int width = 0, height = 0, w, h;
	std::vector<uint8_t> rgba;
	HeaderSize(ink, w, h);
	Check(FillPatternPixels(ink, width, height, rgba) && width == w && height == h
		&& rgba.size() == size_t(w) * h * 4, where + ": decoded to its size");
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		printf("fill_patterns: no documents\n");
		return 1;
	}
	std::string first = argv[1];
	std::string app = first.substr(0, first.rfind('/'));
	app = app.substr(0, app.rfind('/'));
	std::vector<InkSetEntry> sets = AvailableInkSets(nullptr, {app + "/Ink Sets"});
	Check(sets.size() == 7, "the seven ink sets");
	for (const InkSetEntry& e : sets)
		for (size_t k = 0; k < e.set.inks.size(); k++)
			CheckFill(e.set.inks[k], e.set.name + " " + std::to_string(k));
	int setFills = sFills;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (const Property& p : doc.styles.properties) {
			if (p.type != Sig("FCol") && p.type != Sig("PCol") && p.type != Sig("BCol")
					&& p.type != Sig("TCol"))
				continue;
			Ink ink;
			try {
				DocArchive a(p.data);
				a.SetVersions(2, 0, 0x1e);
				ReadInk(a, ink);
			} catch (const ArchiveError&) {
				continue;
			}
			CheckFill(ink, argv[i]);
		}
		for (const auto& kind : doc.inkSets.sets)
			for (const InkSet& set : kind)
				for (const Ink& ink : set.inks)
					CheckFill(ink, std::string(argv[i]) + " set " + set.name);
	}
	Check(setFills >= 30 && sFills > setFills, "the sets' and the documents' fills");

	// the first Clouds swatch: inflated here
	const InkSetEntry* clouds = nullptr;
	for (const InkSetEntry& e : sets)
		if (e.set.name == "Clouds ")
			clouds = &e;
	Check(clouds != nullptr && !clouds->set.inks.empty(), "Clouds");
	if (clouds != nullptr && !clouds->set.inks.empty()) {
		const std::vector<uint8_t>& d = clouds->set.inks[0].otherFill;
		// after the bounds: compressed (46), the packed size (47), data
		uint32_t size = BE32(d, 17), packed = BE32(d, 47), bytesPerRow = BE32(d, 26);
		size_t at = 51;
		while (d[at] == 0)
			at++;
		std::vector<uint8_t> raw(size);
		uLongf length = size;
		int result = uncompress(raw.data(), &length, d.data() + at, packed);
		int width, height;
		std::vector<uint8_t> rgba;
		bool ok = result == Z_OK && FillPatternPixels(clouds->set.inks[0], width, height, rgba);
		for (int y : {0, 17, height - 1})
			for (int x : {0, 40, width - 1}) {
				const uint8_t* p = &raw[size_t(y) * bytesPerRow + size_t(x) * 4];
				const uint8_t* q = &rgba[(size_t(y) * width + x) * 4];
				ok = ok && q[0] == p[2] && q[1] == p[1] && q[2] == p[0] && q[3] == 255;
			}
		Check(ok, "Clouds' first swatch: its pixels");
		printf("Clouds 0: %dx%d, pixel 0,0 = %d,%d,%d\n", width, height, rgba[0], rgba[1], rgba[2]);
	}
	printf("fill_patterns: fills %d (ink sets %d), ok %d, failed %d\n", sFills, setFills, sOk,
		sFailed);
	return sFailed ? 1 : 0;
}
