// Footnotes and endnotes (TMiscFootnoteRef, TMiscFootnoteMark): a
// reference in the text (an 'FREF' paragraph info on a 0x04 character)
// points at the note's frame, a text part of its own (+0x98 1) whose portal
// is in the referring part; the note's text starts with its mark ('FMRK').
#ifndef GOBE_NOTES_H
#define GOBE_NOTES_H

#include <string>

#include "WPPart.h"

namespace gobe {

struct Document;
struct TextPosition;

// the note's text part (its frame's part), -1 if none
int NotePart(const Document& doc, const WPMiscInfo& reference);
// TMiscFootnoteRef::GetMark: its number if numbered automatically, else
// its mark
std::string NoteMark(const WPMiscInfo& reference);
// TMiscFootnoteMark::GetMark: the mark of the reference to note part
// `notePart` ("" if none refers to it)
std::string NoteMarkOf(const Document& doc, size_t notePart);
// TMiscFootnoteRef::Write: the reference's payload from its fields
void WriteNoteReference(WPMiscInfo& reference);
// TWPPart::RenumberFootnotes: the automatically numbered references of
// part `part` (footnotes and endnotes alike) numbered in text order from
// the part's first number (+0x9a). True if a number changed.
bool RenumberNotes(Document& doc, size_t part);
// Footnote Options (TWPSetFootnoteOptionsCmd): the part's first note
// number (+0x9a), and the notes numbered again. False if it didn't change.
bool SetFirstNoteNumber(Document& doc, size_t part, uint16_t first);
// Format > Insert Footnote… (TMiscFootnoteRef::TMiscFootnoteRef): a note
// part (its paragraph in a style based on "Footnote", 10 point, starting
// with its mark and a space) with a frame in part `part`, and the
// reference with a space at `at`, the marks superscript; then the notes
// are numbered again. `mark` empty: numbered automatically. The note's
// part, -1 if it can't be made; `after` is the position after the
// reference.
int InsertNote(Document& doc, size_t part, TextPosition at, bool endnote, const std::string& mark,
	TextPosition& after);

} // namespace gobe

#endif
