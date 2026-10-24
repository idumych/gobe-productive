// doc_notes: Insert Footnote (TMiscFootnoteRef::TMiscFootnoteRef): the
// note's part (+0x98 1, a paragraph style based on "Footnote", 10 point,
// its mark first), its frame in the referring part, the reference and its
// superscript mark, numbering in text order from the part's first number
// (TWPPart::RenumberFootnotes), custom marks, saving and reading back.
// Arguments: none.
#include <cstdio>
#include <string>

#include "Document.h"
#include "Editing.h"
#include "Notes.h"
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


static const WPMiscInfo*
RefAt(const Document& d, size_t paragraph, size_t index)
{
	const WPParagraph& p = d.WordPart(0)->paragraphs[paragraph];
	size_t n = 0;
	for (const WPMiscInfo& m : p.misc)
		if (m.kind == Sig("FREF") && n++ == index)
			return &m;
	return nullptr;
}


static uint32_t
FaceAt(const Document& d, size_t part, size_t paragraph, size_t offset)
{
	const WPParagraph& p = d.WordPart(part)->paragraphs[paragraph];
	uint32_t style = p.parStyle;
	for (const WPStyleRun& r : p.runs)
		if (r.position <= offset)
			style = r.style;
	return ResolveTextStyle(d.styles, style).faceFlags;
}


int
main()
{
	Document doc;
	std::string error, path = "/tmp/doc_notes.gobe";
	CreateBlankDocument(doc, error);
	size_t parts = doc.parts.parts.size();
	InsertText(*doc.EditableWordPart(0), {0, 0}, "Hello world");
	TextPosition after;
	int note = InsertNote(doc, 0, {0, 5}, false, "", after);
	Check(note == int(parts) && after.paragraph == 0 && after.offset == 7, "a note part, after 5 + 2");
	const WPParagraph& p = doc.WordPart(0)->paragraphs[0];
	Check(p.text.substr(0, 7) == "Hello\x04 " && p.misc.size() == 1 && p.misc[0].position == 5,
		"the reference and a space at 5");
	const WPMiscInfo* ref = RefAt(doc, 0, 0);
	Check(ref != nullptr && ref->frame[0] == uint32_t(note) && ref->frame[1] == 0
		&& ref->frame[2] == 0 && !ref->endnote && ref->autoNumber && ref->number == 1
		&& NoteMark(*ref) == "1", "numbered 1 (the part's first number)");
	Check((FaceAt(doc, 0, 0, 5) & 0x40) && !(FaceAt(doc, 0, 0, 6) & 0x40)
		&& !(FaceAt(doc, 0, 0, 4) & 0x40), "only the mark is superscript");
	const WPPart* wp = doc.WordPart(size_t(note));
	Check(wp != nullptr && wp->version98 == 1 && wp->paragraphs[0].text == "\x04 "
		&& wp->paragraphs[0].misc.size() == 1 && wp->paragraphs[0].misc[0].kind == Sig("FMRK")
		&& (FaceAt(doc, size_t(note), 0, 0) & 0x40), "the note starts with its mark");
	if (wp != nullptr) {
		const Style& par = doc.styles.styles[wp->paragraphs[0].parStyle];
		Check(doc.styles.styles[par.links[0]].name == "Footnote"
			&& ResolveTextStyle(doc.styles, wp->paragraphs[0].parStyle).size == 10,
			"in a style based on \"Footnote\", 10 point");
	}
	const Portal& portal = doc.tails[size_t(note)].portals[0];
	Check(portal.part == 0 && portal.frames.size() == 1 && portal.frames[0].container == 0
		&& (portal.frames[0].values[3] & 8), "its frame is in the text part, growing");
	Check(NoteMarkOf(doc, size_t(note)) == "1", "the note's mark");
	// a note before it: numbered 1, the other 2; an endnote with its own mark
	int first = InsertNote(doc, 0, {0, 0}, false, "", after);
	int star = InsertNote(doc, 0, {0, 15}, true, "*", after);
	Check(first > note && star > first && RefAt(doc, 0, 0)->number == 1
		&& RefAt(doc, 0, 1)->number == 2 && NoteMarkOf(doc, size_t(note)) == "2"
		&& NoteMark(*RefAt(doc, 0, 2)) == "*" && RefAt(doc, 0, 2)->endnote,
		"renumbered in text order, a custom mark");
	// Footnote Options: numbered from 5
	Check(SetFirstNoteNumber(doc, 0, 5) && RefAt(doc, 0, 0)->number == 5
		&& RefAt(doc, 0, 1)->number == 6 && !SetFirstNoteNumber(doc, 0, 5), "numbered from 5");
	Check(doc.Save(path, error), "save " + error);
	ref = RefAt(doc, 0, 1);
	Check(doc.parts.parts.size() == parts + 3 && ref != nullptr && ref->number == 6
		&& doc.WordPart(0)->version9a == 5
		&& ref->frame[0] == uint32_t(note) && NoteMark(*RefAt(doc, 0, 2)) == "*"
		&& RefAt(doc, 0, 2)->endnote && !RefAt(doc, 0, 2)->autoNumber,
		"saved and read back");
	Check(doc.WordPart(size_t(star)) != nullptr
		&& doc.WordPart(size_t(star))->paragraphs[0].misc[0].kind == Sig("FMRK"),
		"the endnote's part read back");
	remove(path.c_str());
	printf("notes ok %d, failed %d\n", sOk, sFailed);
	return sFailed > 0 ? 1 : 0;
}
