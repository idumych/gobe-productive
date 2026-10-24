// doc_docsize: Document > Document Size… for text (TWPSetDocSizeCmd). In
// each paged sample with a text main part: two pages across and Title Page
// set; the pages placed as TPagedRootPart::GetPaperPosition places them
// (page k at (paper width + 20) * (k % 2), (paper height + 20) * (k / 2));
// the same again changes nothing; saved and loaded, both stay; set back,
// the file's bytes are the original's.
// Arguments: the sample documents.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "Document.h"

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


static std::vector<char>
Bytes(const std::string& path)
{
	std::ifstream f(path, std::ios::binary);
	return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}


int
main(int argc, char** argv)
{
	int docs = 0;
	for (int i = 1; i < argc; i++) {
		std::string name = argv[i];
		name = name.substr(name.rfind('/') + 1);
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error) || doc.PagePart() < 0 || doc.MainPart() < 0
				|| doc.WordPart(size_t(doc.MainPart())) == nullptr)
			continue;
		docs++;
		size_t main = size_t(doc.MainPart());
		uint32_t across = doc.PagesAcrossCount();
		bool title = doc.TitlePage(main);
		Check(across == 1, name + ": one page across (" + std::to_string(across) + ")");
		while (doc.PageCount() < 3)
			doc.AddPage();
		Check(doc.SetDocumentSize(main, 2, true), name + ": set");
		Check(doc.PagesAcrossCount() == 2 && doc.TitlePage(main), name + ": two across, title page");
		Check(!doc.SetDocumentSize(main, 2, true), name + ": the same again changes nothing");
		const float* paper = doc.sheets.sheets[0].paper;
		float w = paper[2] - paper[0], h = paper[3] - paper[1];
		const Portal& pages = doc.tails[size_t(doc.PagePart())].portals[0];
		bool placed = true;
		for (size_t k = 0; k < pages.frames.size(); k++)
			placed = placed && pages.frames[k].origin[0] == std::nearbyint((w + 20) * float(k % 2))
				&& pages.frames[k].origin[1] == std::nearbyint((h + 20) * float(k / 2));
		Check(placed, name + ": pages two across");
		std::string path = "/tmp/doc_docsize.gobe";
		Document again;
		Check(doc.Save(path, error) && LoadDocument(path, again, error)
			&& again.PagesAcrossCount() == 2 && again.TitlePage(main),
			name + ": saved and loaded (" + error + ")");

		Document plain;
		LoadDocument(argv[i], plain, error);
		plain.SetDocumentSize(main, 2, !title);
		plain.SetDocumentSize(main, 1, title);
		std::string back = "/tmp/doc_docsize_back.gobe";
		Check(plain.Save(back, error) && Bytes(back) == Bytes(argv[i]),
			name + ": set back, the same bytes");
	}
	printf("docsize: documents %d, ok %d, failed %d\n", docs, sOk, sFailed);
	return sFailed > 0;
}
