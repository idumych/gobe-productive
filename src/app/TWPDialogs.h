// Word processing dialogs: Insert Date/Time (TDialogWPDateTimeFormat),
// Insert Formula (TDialogWPFormula), Columns (TDialogSection), Insert
// Footnote (TDialogWPInsertFootnote, TDialogWPFootnoteOptions), Paragraph
// (TDialogFormatParagraph).
#ifndef GOBE_TWP_DIALOGS_H
#define GOBE_TWP_DIALOGS_H

#include <string>
#include <vector>

#include <String.h>

#include "TDialog.h"
#include "WordCount.h"

class BLooper;

namespace gobe {

// Format > Insert Date… / Insert Time… (DIAL/Item 302, "Date Format" or
// "Time Format"): five formats (STR# 1307 dates, 1308 times), each radio
// showing now in it, and "Static"; OK sends the window the command
// ('sM53' or 'sM54') with "format" and "static". `format` is the field's
// format when it is edited (the last one is chosen if none matches).
class TDialogWPDateTimeFormat : public TDialog {
public:
	TDialogWPDateTimeFormat(BLooper* window, uint32 command, const std::string& format,
		bool isStatic);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	std::string Format(int32 index) const;

	BLooper* fWindow;
	uint32 fCommand;
	std::string fFormat;
	bool fStatic;
};

// Format > Insert Formula… (DIAL/Item 300): the formula and whether the
// field shows its result, its formula or both; OK (only with a formula)
// checks that it parses ("=" put in front if missing) and sends the window
// 'sM55' with "formula", "format" ("General"), "displayFormula" and
// "displayResult".
class TDialogWPFormula : public TDialog {
public:
	TDialogWPFormula(BLooper* window, const std::string& formula, bool displayResult,
		bool displayFormula);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	std::string fFormula;
	bool fDisplayResult, fDisplayFormula;
};

// Format > Columns… (DIAL/Item 303): the number of columns of the
// section, 1 to 10 (else STR# 6, 13); OK sends the window 'sM51' with it
// as "value".
class TDialogSection : public TDialog {
public:
	TDialogSection(BLooper* window, int32 columns);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	int32 fColumns;
};

// Format > Insert Footnote… (DIAL/Item 306): a footnote or an endnote
// (labels STR# 1302, 10-13: "at End of Page"/"of Document" in the main
// text, "of Frame"/"of Text" elsewhere; the last choice is kept), numbered
// automatically or with a custom mark; OK sends the window 'sM5I' with
// "endnote" and "mark". Options… opens the Footnote Options.
class TDialogWPInsertFootnote : public TDialog {
public:
	TDialogWPInsertFootnote(BLooper* window, int16 first, bool mainText);

	virtual void MessageReceived(BMessage* message);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	int16 fFirst;
	bool fMainText;
};

// Footnote Options (DIAL/Item 307): the starting number; OK sends the
// window 'sM5K' with "start".
class TDialogWPFootnoteOptions : public TDialog {
public:
	TDialogWPFootnoteOptions(BLooper* window, int16 first);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	int16 fFirst;
};

// Format > Paragraph… (DIAL/Item 304, "Paragraph Format"): left, first
// line and right indents (0 up to the frame's width less the other side's
// indent and 8) and line spacing (1 point to 160), space before and after
// (0 to 160), each in a unit of its own (MENU 241; the indents have no
// lines), and the alignment (MENU 300). OK sends the window 'sM50' with the
// values in points ("left", "first", "right", "lineSpacing", "spaceBefore",
// "spaceAfter"), whether a spacing is in lines ("...Lines") and "align".
class TDialogFormatParagraph : public TDialog {
public:
	// the paragraph's values (spacings in lines when `...Lines`), the
	// sheet's unit and the frame's width
	TDialogFormatParagraph(BLooper* window, const float values[6], const bool lines[3],
		int32 align, int unit, float width);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	struct Value {
		float points;
		int unit;
	};
	bool ValidTextControl(int32 index);
	bool ValidTextControls();
	Value& ValueOf(int32 index);
	void ShowValue(int32 index);

	BLooper* fWindow;
	Value fValues[6];		// items 2, 4, 6, 8, 10, 12
	int32 fAlign;
	float fWidth;
	std::vector<std::string> fUnitNames;
};

// A size typed in (DIAL/Item 31, TDialogGenericGetString with
// TDialogGRSize::ValidateAndSendMsg): for 'sf0K' (Size > Other…) 2 to 510,
// else STR# 6, 13; OK sends the window the command with the size as a
// float "value".
class TDialogGRSize : public TDialog {
public:
	TDialogGRSize(BLooper* window, uint32 command, const char* title, const char* label,
		const char* text);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	uint32 fCommand;
	BString fTitle, fLabel, fText;
};

// Writing Tools > Word Count… (DIAL/Item 54): the counts; for a
// selection only characters and words.
class TWordCountDialog : public TDialog {
public:
	TWordCountDialog(const WordCount& count, bool selection);

protected:
	virtual void Init();

private:
	WordCount fCount;
	bool fSelection;
};

} // namespace gobe

#endif
