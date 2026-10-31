// ink_sets: inks as the original writes them (TPenInk::Write) and its
// pattern and gradient sets (BPAT 0, GRAD 0). Every ink property of the
// documents (text, fill, pen colors; a border color's ink before its
// side) read and written again must give back the bytes read: solid,
// pattern, directional and radial gradient inks. The application's sets
// read: their size, the first pattern, the gradients' kinds.
// Arguments: documents (the original program is found next to Samples).
#include <cstdio>
#include <string>

#include "DocArchive.h"
#include "Document.h"
#include "InkSets.h"
#include "ResourceFile.h"
#include "Styles.h"

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


int
main(int argc, char** argv)
{
	size_t solid = 0, patterns = 0, gradients = 0;
	std::string program;
	for (int i = 1; i < argc; i++) {
		std::string path = argv[i];
		size_t samples = path.find("/Samples/");
		if (program.empty() && samples != std::string::npos)
			program = path.substr(0, samples) + "/Gobe Productive";
		Document doc;
		std::string error;
		if (!LoadDocument(path, doc, error))
			continue;
		for (const Property& p : doc.styles.properties) {
			bool border = p.type == Sig("BCol");
			if (p.type != Sig("TCol") && p.type != Sig("FCol") && p.type != Sig("PCol") && !border)
				continue;
			std::vector<uint8_t> bytes(p.data.begin(), p.data.end() - (border ? 4 : 0));
			DocArchive a(bytes);
			Ink ink;
			try {
				ReadInk(a, ink);
			} catch (const ArchiveError&) {
				Check(false, path + ": an ink read");
				continue;
			}
			if (ink.gradient)
				gradients++;
			else if (ink.pattern[0] != 0xff || ink.pattern[7] != 0xff)
				patterns++;
			else
				solid++;
			// (BusinessCards' bitmap fill has a byte past its size: only what
			// ReadInk took)
			std::vector<uint8_t> read(bytes.begin(), bytes.begin() + long(a.Position()));
			Check(InkBytes(ink) == read && bytes.size() - read.size() <= 1,
				path + ": an ink written as it was read");
		}
	}
	Check(gradients > 0 && patterns > 0 && solid > 0, "solid, pattern and gradient inks seen");

	ResourceFile resources;
	Check(!program.empty() && resources.Load(program), "the program's resources " + program);
	const ResourceFile::Resource* bpat = resources.Find(FourCC("BPAT"), 0);
	const ResourceFile::Resource* grad = resources.Find(FourCC("GRAD"), 0);
	Check(bpat != nullptr && grad != nullptr, "BPAT 0 and GRAD 0");
	if (bpat != nullptr) {
		InkSet set;
		Check(ParsePatternSet(bpat->data, set), "the pattern set");
		// "Black White": 6 columns of 20-pixel cells; the first pattern
		// black on white, solid
		Check(set.columns == 6 && set.cell == 20 && set.inks.size() == (bpat->data.size() - 8) / 64,
			"its layout: " + std::to_string(set.columns) + " columns, " + std::to_string(set.inks.size()));
		if (!set.inks.empty()) {
			const Ink& first = set.inks[0];
			Check(!first.gradient && first.color.r == 0 && first.color.a == 255
				&& first.low.r == 255 && first.low.g == 255 && first.low.b == 255,
				"the first pattern black on white");
			// written and read again
			std::vector<uint8_t> bytes = InkBytes(first);
			DocArchive a(bytes);
			Ink back;
			ReadInk(a, back);
			Check(InkBytes(back) == bytes && bytes.size() == 17, "a pattern ink's 17 bytes");
		}
	}
	if (grad != nullptr) {
		InkSet set;
		Check(ParseGradientSet(grad->data, set), "the gradient set");
		size_t radial = 0, directional = 0;
		for (const Ink& ink : set.inks) {
			directional += ink.gradientType == Sig("GDdr");
			radial += ink.gradientType == Sig("GDrl");
			Check(ink.colorCount >= 2 && ink.colorCount <= 4, "a gradient's color count");
		}
		Check(set.columns == 6 && set.cell == 20 && directional > 0
			&& directional + radial == set.inks.size(), "its gradients: " + std::to_string(directional)
			+ " directional, " + std::to_string(radial) + " radial");
		// the second: two colors, 90 degrees (the resource's 0x5a)
		if (set.inks.size() > 1)
			Check(set.inks[1].colorCount == 2 && set.inks[1].angle == 90, "the second at 90 degrees");
	}
	printf("inks: %zu solid, %zu pattern, %zu gradient\n", solid, patterns, gradients);
	printf("ink_sets: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
