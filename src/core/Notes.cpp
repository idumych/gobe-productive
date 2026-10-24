// Footnotes and endnotes.
#include "Notes.h"

#include <algorithm>

#include "Document.h"
#include "DocWriter.h"
#include "Editing.h"
#include "Styles.h"

namespace gobe {

int
NotePart(const Document& doc, const WPMiscInfo& reference)
{
	if (reference.kind != Sig("FREF") || reference.frame[0] >= doc.parts.parts.size())
		return -1;
	return int(reference.frame[0]);
}


std::string
NoteMark(const WPMiscInfo& reference)
{
	return reference.autoNumber ? std::to_string(reference.number) : reference.text;
}


std::string
NoteMarkOf(const Document& doc, size_t notePart)
{
	for (const auto& [part, wp] : doc.wordParts)
		for (const WPParagraph& p : wp.paragraphs)
			for (const WPMiscInfo& m : p.misc)
				if (m.kind == Sig("FREF") && m.frame[0] == notePart)
					return NoteMark(m);
	return "";
}


void
WriteNoteReference(WPMiscInfo& reference)
{
	DocWriter w;
	for (uint32_t v : reference.frame)
		w.UInt32(v);
	w.Bool(reference.endnote);
	w.Bool(reference.autoNumber);
	w.UInt32(reference.number);
	w.String(reference.autoNumber ? std::string() : reference.text);
	reference.raw = w.Data();
}


bool
RenumberNotes(Document& doc, size_t part)
{
	const WPPart* read = doc.WordPart(part);
	if (read == nullptr)
		return false;
	// the paragraph infos are kept in text order
	uint32_t number = read->version9a;
	bool changed = false;
	for (const WPParagraph& p : read->paragraphs)
		for (const WPMiscInfo& m : p.misc)
			if (m.kind == Sig("FREF") && m.autoNumber)
				changed = changed || m.number != number++;
	if (!changed)
		return false;
	WPPart* wp = doc.EditableWordPart(part);
	number = wp->version9a;
	for (WPParagraph& p : wp->paragraphs)
		for (WPMiscInfo& m : p.misc)
			if (m.kind == Sig("FREF") && m.autoNumber) {
				m.number = number++;
				WriteNoteReference(m);
			}
	return true;
}


bool
SetFirstNoteNumber(Document& doc, size_t part, uint16_t first)
{
	const WPPart* read = doc.WordPart(part);
	if (read == nullptr || read->version9a == first || part >= doc.parts.parts.size())
		return false;
	// the part record keeps it after 'wp++' and +0x98 (written as read)
	std::vector<uint8_t>& record = doc.parts.parts[part].data;
	size_t at = std::string(record.begin(), record.end()).find("wp++");
	if (at == std::string::npos || at + 8 > read->bodyStart)
		return false;
	record[at + 6] = uint8_t(first >> 8);
	record[at + 7] = uint8_t(first);
	doc.EditableWordPart(part)->version9a = first;
	RenumberNotes(doc, part);
	return true;
}


namespace {

// TWPPart::InsertMiscCharPrim for a note's reference or mark: the
// character and a space, the paragraph info on the character, which is
// made superscript (as the text there, face 0x40 added)
TextPosition
InsertNoteCharacter(Document& doc, size_t part, TextPosition at, WPMiscInfo info)
{
	WPPart* wp = doc.EditableWordPart(part);
	if (wp == nullptr || at.paragraph >= wp->paragraphs.size())
		return at;
	at.offset = std::min(at.offset, EditableLength(wp->paragraphs[at.paragraph]));
	info.position = uint32_t(at.offset);
	TextPosition after = InsertText(*wp, at, "\x04 ");
	std::vector<WPMiscInfo>& misc = wp->paragraphs[at.paragraph].misc;
	auto it = std::find_if(misc.begin(), misc.end(),
		[&](const WPMiscInfo& m) { return m.position > info.position; });
	misc.insert(it, std::move(info));
	const WPParagraph& p = wp->paragraphs[at.paragraph];
	uint32_t style = p.parStyle;
	for (const WPStyleRun& r : p.runs)
		if (r.position <= at.offset)
			style = r.style;
	uint32_t flags = (ResolveTextStyle(doc.styles, style).faceFlags | 0x40) & ~uint32_t(1);
	ApplyCharacterProperty(doc, part, at, {at.paragraph, at.offset + 1}, TextFaceProperty(flags));
	return after;
}

} // namespace


int
InsertNote(Document& doc, size_t part, TextPosition at, bool endnote, const std::string& mark,
	TextPosition& after)
{
	after = at;
	if (doc.WordPart(part) == nullptr || part >= doc.tails.size() || !doc.tailRead[part])
		return -1;
	// the note's frame in the style of the part's first frame
	uint32_t frameStyle = kNoPart;
	for (const Portal& portal : doc.tails[part].portals)
		if (!portal.frames.empty()) {
			frameStyle = portal.frames[0].style;
			break;
		}
	uint32_t footnote = doc.NamedParagraphStyle("Footnote", {TextSizeProperty(10)});
	if (footnote == kNoPart || frameStyle >= doc.styles.styles.size())
		return -1;
	const float origin[2] = {0, 0}, extent[2] = {0, 0};
	int note = doc.NewTextPart(1, footnote, uint32_t(part), origin, extent, frameStyle);
	if (note < 0)
		return -1;
	WPMiscInfo markInfo{};
	markInfo.kind = Sig("FMRK");
	InsertNoteCharacter(doc, size_t(note), {0, 0}, markInfo);
	WPMiscInfo reference{};
	reference.kind = Sig("FREF");
	reference.frame[0] = uint32_t(note);
	reference.frame[1] = 0;
	reference.frame[2] = 0;
	reference.endnote = endnote;
	reference.autoNumber = mark.empty();
	reference.text = mark;
	WriteNoteReference(reference);
	after = InsertNoteCharacter(doc, part, at, reference);
	RenumberNotes(doc, part);
	return note;
}

} // namespace gobe
