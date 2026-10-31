// style_archive: Import and Export Styles. For each document: its named
// styles copied (CopyStyles, as Export Styles marks them), archived as
// "Style Definition" messages and restored, must come back as they were,
// every property byte for byte; pasted into a new document, each style
// must resolve to the same text style as in the document. The styles
// file: written and read back, a later version refused, other files
// refused.
// Arguments: documents.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "DocArchive.h"
#include "Document.h"
#include "Editing.h"
#include "StyleArchive.h"
#include "StyleEdit.h"
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


static bool
SameText(const TextStyle& a, const TextStyle& b)
{
	return a.family == b.family && a.face == b.face && a.size == b.size
		&& a.faceFlags == b.faceFlags && a.color.r == b.color.r && a.color.g == b.color.g
		&& a.color.b == b.color.b && a.alignment == b.alignment
		&& a.leftIndent == b.leftIndent && a.firstIndent == b.firstIndent
		&& a.rightIndent == b.rightIndent && a.spaceBefore == b.spaceBefore
		&& a.spaceAfter == b.spaceAfter && a.lineSpacing == b.lineSpacing
		&& a.tabs.size() == b.tabs.size();
}


int
main(int argc, char** argv)
{
	size_t mapped = 0, raw = 0, documents = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		documents++;
		std::vector<std::string> names;
		for (const Style& s : doc.styles.styles)
			if (!s.name.empty() && !(s.value8 & 0x400))
				names.push_back(s.name);
		if (names.empty())
			continue;
		std::vector<StyleDefinition> copied = CopyStyles(doc.styles, names);
		ArchiveMessage archive = ArchiveStyles(copied);
		std::vector<StyleDefinition> restored = RestoreStyles(archive);
		Check(restored.size() == copied.size(), std::string(argv[i]) + ": "
			+ std::to_string(restored.size()) + " styles restored of "
			+ std::to_string(copied.size()));
		for (size_t k = 0; k < copied.size() && k < restored.size(); k++) {
			const StyleDefinition& a = copied[k];
			const StyleDefinition& b = restored[k];
			bool same = a.name == b.name && a.type == b.type && a.level == b.level
				&& a.flags == b.flags && a.base == b.base && a.parent == b.parent
				&& a.owner == b.owner;
			// a PTab property with several tabs comes back as one per tab
			std::vector<uint8_t> tabsA, tabsB;
			std::vector<Property> restA, restB;
			for (const Property& p : a.properties)
				if (p.type == Sig("PTab"))
					tabsA.insert(tabsA.end(), p.data.begin(), p.data.end());
				else
					restA.push_back(p);
			for (const Property& p : b.properties)
				if (p.type == Sig("PTab"))
					tabsB.insert(tabsB.end(), p.data.begin(), p.data.end());
				else
					restB.push_back(p);
			same = same && tabsA == tabsB && restA.size() == restB.size();
			for (size_t q = 0; same && q < restA.size(); q++)
				same = restA[q].type == restB[q].type && restA[q].data == restB[q].data;
			Check(same, std::string(argv[i]) + ": style \"" + a.name + "\" restored as it was");
		}
		// how many properties went through their class's fields
		const ArchiveMessage::Value* first = nullptr;
		for (size_t k = 0; (first = archive.Find("Style Definition", kArchiveMessage, k)); k++)
			for (size_t q = 0; q < first->message[0].Count("Style Property"); q++) {
				const ArchiveMessage& p
					= first->message[0].Find("Style Property", kArchiveMessage, q)->message[0];
				if (p.Find("Property Data", kArchiveRaw) != nullptr)
					raw++;
				else
					mapped++;
			}

		// imported into a new document: the same text styles
		Document fresh;
		if (!CreateBlankDocument(fresh, error)) {
			Check(false, "a new document: " + error);
			continue;
		}
		PasteStyles(fresh, restored);
		for (const std::string& name : names) {
			uint32_t was = FindNamedStyle(doc.styles, name);
			uint32_t is = FindNamedStyle(fresh.styles, name);
			if (is >= fresh.styles.styles.size()) {
				Check(false, std::string(argv[i]) + ": \"" + name + "\" imported");
				continue;
			}
			Check(SameText(ResolveTextStyle(doc.styles, was), ResolveTextStyle(fresh.styles, is)),
				std::string(argv[i]) + ": \"" + name + "\" looks the same imported");
		}
	}

	// pens of each kind and a gradient ink (kept raw), built by hand as
	// TPen::WritePenData and its subclasses lay them out
	{
		auto pen = [](const char* kind, const std::vector<uint8_t>& rest) {
			// present, the kind, the size, the name "P", printed, the rest
			std::vector<uint8_t> d = {0xff, 0xff, 0xff, 0xff, uint8_t(kind[0]), uint8_t(kind[1]),
				uint8_t(kind[2]), uint8_t(kind[3]), 0, 0, 0, uint8_t(5 + rest.size()),
				0, 1, 'P', 0, 1};
			d.insert(d.end(), rest.begin(), rest.end());
			return Property{Sig("PEN "), d};
		};
		const std::vector<uint8_t> two = {0x40, 0, 0, 0}, three = {0x40, 0x40, 0, 0};
		StyleDefinition s;
		s.name = "Pens";
		s.type = 3;
		s.flags = kStyleCopied;
		s.properties.push_back(pen("NORM", {}));
		std::vector<uint8_t> dash = two;
		dash.insert(dash.end(), three.begin(), three.end());
		s.properties.push_back(pen("DASH", dash));
		std::vector<uint8_t> dot = two;
		dot.push_back(1);
		s.properties.push_back(pen("PDOT", dot));
		s.properties.push_back(pen("HASH", three));
		s.properties.push_back(Property{Sig("PEN "), {0, 0, 0, 0}});
		// a right tab with a dotted fill at 1 inch (TPropParTab), a label
		s.properties.push_back(TabProperty(TabStop{72, 2, 1, '.', '-'}));
		s.properties.push_back(Property{Sig("PLab"), {0, 3, 0, 1, 'C', 0, 0, 0, 0}});
		// a gradient fill ink: not mapped, so kept as it is
		s.properties.push_back(Property{Sig("FCol"), {0, 'F', 'G', 'R', 'D', 0, 0, 0, 1, 9}});
		ArchiveMessage archive = ArchiveStyles({s});
		const ArchiveMessage& m = archive.Find("Style Definition", kArchiveMessage)->message[0];
		const ArchiveMessage& dashed = m.Find("Style Property", kArchiveMessage, 1)->message[0];
		const ArchiveMessage::Value* width = dashed.Find("Dash Width", kArchiveFloat);
		const ArchiveMessage::Value* gap = dashed.Find("Gap Width", kArchiveFloat);
		Check(width != nullptr && width->numbers[0] == 2 && gap != nullptr && gap->numbers[0] == 3,
			"a dashed pen's widths as fields");
		Check(m.Find("Style Property", kArchiveMessage, 7)->message[0].Find("Property Data",
			kArchiveRaw) != nullptr, "a gradient ink kept raw");
		std::vector<StyleDefinition> back = RestoreStyles(archive);
		bool same = back.size() == 1 && back[0].properties.size() == s.properties.size();
		for (size_t k = 0; same && k < s.properties.size(); k++)
			same = back[0].properties[k].data == s.properties[k].data;
		Check(same, "pens and a raw ink restored byte for byte");
	}

	// the file
	std::vector<uint8_t> header(98, 0);
	std::string id = "GOBE SCOTT/01/02DOC!";
	std::copy(id.begin(), id.end(), header.begin());
	std::vector<uint8_t> flat = {1, 2, 3, 4, 5, 0, 0xff};
	std::vector<uint8_t> file = StylesFileBytes(header, flat);
	Check(file.size() == 98 + 4 + 4 + 4 + flat.size() + 4 + 4, "the file's size: "
		+ std::to_string(file.size()));
	Check(std::string(file.begin() + 16, file.begin() + 20) == "STYL", "the file's type");
	std::vector<uint8_t> back;
	bool newer = true;
	std::string error;
	Check(ReadStylesFile(file, back, newer, error) && back == flat && !newer,
		"the message read back " + error);
	std::vector<uint8_t> later = file;
	later[98 + 3] = 2;
	Check(!ReadStylesFile(later, back, newer, error) && newer, "version 2 refused as newer");
	std::vector<uint8_t> document = file;
	document[16] = 'D';
	Check(!ReadStylesFile(document, back, newer, error) && !newer, "a document isn't a styles file");
	std::vector<uint8_t> cut(file.begin(), file.end() - 6);
	Check(!ReadStylesFile(cut, back, newer, error), "a cut file refused");

	printf("documents %zu, properties mapped %zu, raw %zu\n", documents, mapped, raw);
	printf("style_archive: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
