// inline_parts: tables and pictures made in a document's text
// (InlineParts: AddTablePart, AddImagePart, InsertInlineFrame). A new
// document gets a 2 x 2 table and LogoLetter's logo in its text; the
// table's cells and frames, the picture and the inline frames are
// checked, then the document is saved and opened again: everything must
// read back (the records as the original lays them out) and write the
// same HTML.
// Arguments: documents (LogoLetter's logo is used).
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "HTMLExport.h"
#include "InlineParts.h"
#include "PartTail.h"

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


static std::string
CellText(const Document& doc, size_t table, size_t row, size_t column)
{
	int p = CellParagraph(doc, table, row, column);
	const TablePart* t = doc.Table(table);
	if (p < 0 || t == nullptr)
		return "?";
	std::string text = doc.WordPart(t->textPart)->paragraphs[size_t(p)].text;
	if (!text.empty() && text.back() == '\n')
		text.pop_back();
	return text;
}


int
main(int argc, char** argv)
{
	const IMPart* logo = nullptr;
	Document source;
	std::string error;
	for (int i = 1; i < argc && logo == nullptr; i++)
		if (std::string(argv[i]).find("LogoLetter") != std::string::npos
				&& LoadDocument(argv[i], source, error) && !source.imageParts.empty())
			logo = &source.imageParts[0].second;
	Check(logo != nullptr, "LogoLetter's logo");

	Document doc;
	Check(CreateBlankDocument(doc, error), "a new document " + error);
	size_t main = size_t(doc.MainPart());
	size_t partsBefore = doc.parts.parts.size(), selectionsBefore = doc.selections.selections.size();
	InsertMultiline(*doc.EditableWordPart(main), {0, 0}, "Before the table.\nAfter it.");

	NewTable table;
	table.columns = {100, 150};
	table.rows = {20, 34};
	table.cells = {{"one", "two"}, {"three", "four\nfive"}};
	size_t text = 0;
	int t = AddTablePart(doc, main, table, text, error);
	Check(t >= 0, "a table part " + error);
	if (t < 0)
		return 1;
	Check(doc.parts.parts.size() == partsBefore + 2 && doc.parts.parts[size_t(t)].kind == Sig("TBLE")
		&& doc.parts.parts[text].kind == Sig("WORD"), "a table part and its text part");
	const TablePart* tp = doc.Table(size_t(t));
	Check(tp != nullptr && tp->rows.size() == 2 && tp->rows[0].cells.size() == 2
		&& tp->rows[1].cells[1].origin[0] == 100 && tp->rows[1].cells[1].origin[1] == 20
		&& tp->rows[1].cells[1].size[0] == 150 && tp->rows[1].cells[1].size[1] == 34
		&& tp->size[0] == 250 && tp->size[1] == 54 && tp->textPart == text, "the table read back");
	Check(CellText(doc, size_t(t), 0, 0) == "one" && CellText(doc, size_t(t), 0, 1) == "two"
		&& CellText(doc, size_t(t), 1, 0) == "three" && CellText(doc, size_t(t), 1, 1) == "four",
		"the cells' first paragraphs");
	if (tp != nullptr && tp->rows.size() == 2 && tp->rows[1].cells.size() == 2) {
		const Frame* last = FrameAt(doc, tp->rows[1].cells[1].frame);
		Check(last != nullptr && last->origin[0] == 102 && last->origin[1] == 21
			&& last->extent[0] == 146 && last->extent[1] == 32 && last->container == size_t(t),
			"the last cell's frame, inset 2 and 1");
		const WPPart* cells = doc.WordPart(text);
		Check(last != nullptr && cells != nullptr && cells->paragraphs.size() == 5
			&& last->textLength == cells->paragraphs[3].text.size() + cells->paragraphs[4].text.size(),
			"the last cell's two paragraphs");
	}
	TextPosition after = InsertInlineFrame(doc, main, {0, 17}, size_t(t));
	Check(after.paragraph == 0 && after.offset == 18, "the table's frame in the text");

	int picture = -1;
	if (logo != nullptr) {
		picture = AddImagePart(doc, main, logo->image, error);
		Check(picture >= 0, "a picture " + error);
		if (picture >= 0) {
			InsertInlineFrame(doc, main, {1, 0}, size_t(picture));
			const IMPart* im = doc.ImagePart(size_t(picture));
			Check(im != nullptr && im->image.bits == logo->image.bits && im->size[0] == 159,
				"the picture's bits");
		}
	}
	Check(doc.selections.selections.size() == selectionsBefore + (picture >= 0 ? 2 : 1),
		"a selection for the text and the picture");
	std::vector<HTMLImage> images;
	std::string html = HTMLText(doc, "t", &images);
	Check(html.find("<TABLE WIDTH=250 HEIGHT=54") != std::string::npos
		&& html.find("<P>four</P>\n<P>five</P>") != std::string::npos,
		"the table in HTML:\n" + html);
	Check(images.size() == (picture >= 0 ? 1u : 0u), "the picture in HTML");

	// saved and opened again
	std::string path = "/tmp/inline_parts_test.gobe";
	Check(doc.Save(path, error), "saved " + error);
	Document back;
	Check(LoadDocument(path, back, error), "opened again " + error);
	Check(back.parts.parts.size() == doc.parts.parts.size() && back.Table(size_t(t)) != nullptr,
		"the parts read back");
	Check(CellText(back, size_t(t), 1, 0) == "three", "a cell's text read back");
	if (picture >= 0) {
		const IMPart* im = back.ImagePart(size_t(picture));
		Check(im != nullptr && logo != nullptr && im->image.bits == logo->image.bits,
			"the picture's bits read back");
	}
	std::vector<HTMLImage> backImages;
	Check(HTMLText(back, "t", &backImages) == html && backImages.size() == images.size()
		&& (images.empty() || backImages[0].bytes == images[0].bytes), "the same HTML after saving");
	const WPPart* backMain = back.WordPart(size_t(back.MainPart()));
	size_t frames = 0;
	for (const WPParagraph& p : backMain->paragraphs)
		for (const WPMiscInfo& m : p.misc)
			frames += m.kind == Sig("FRAM") && FrameAt(back, m.frame) != nullptr;
	Check(frames == (picture >= 0 ? 2u : 1u), "the inline frames read back");

	printf("inline_parts: ok %d, failed %d\n", sOk, sFailed);
	return sFailed ? 1 : 0;
}
