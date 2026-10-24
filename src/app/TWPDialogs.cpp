// Word processing dialogs: Insert Date/Time (TDialogWPDateTimeFormat),
// Insert Formula (TDialogWPFormula), Columns (TDialogSection), Insert
// Footnote (TDialogWPInsertFootnote, TDialogWPFootnoteOptions), Paragraph
// (TDialogFormatParagraph), the ruler's Edit Tab (TDialogTabEdit) and Set
// Ruler Divisions (TDialogSetRulerDivisions).
#include "TWPDialogs.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <Alert.h>
#include <Button.h>
#include <Looper.h>
#include <Message.h>
#include <TextControl.h>

#include <functional>

#include "Editing.h"
#include "GobeResources.h"
#include "Measure.h"
#include "SSEval.h"
#include "SSStyle.h"
#include "TWPRuler.h"

namespace gobe {

TDialogWPDateTimeFormat::TDialogWPDateTimeFormat(BLooper* window, uint32 command,
	const std::string& format, bool isStatic)
	:
	TDialog(302, 302, 'APPL', NULL),
	fWindow(window),
	fCommand(command),
	fFormat(format),
	fStatic(isStatic)
{
}


// TDialogWPDateTimeFormat::GetFormat
std::string
TDialogWPDateTimeFormat::Format(int32 index) const
{
	return GobeResources::IndexString(fCommand == 'sM53' ? 0x51b : 0x51c, index).String();
}


void
TDialogWPDateTimeFormat::Init()
{
	if (fCommand != 'sM53')
		SetTitle(GobeResources::IndexString(0x516, 9).String());
	// each format shows "=Now()" in it
	SSValue now = SSValue::Time(int64_t(time(NULL)));
	int32 chosen = 7;
	for (int32 item = 3; item <= 7; item++) {
		std::string format = Format(item - 3);
		SetLabelItem(item, FormatCellValue(now, format).c_str());
		if (format == fFormat)
			chosen = item;
	}
	SetRadioItem(chosen, true);
	SetCheckBoxItem(8, fStatic);
}


bool
TDialogWPDateTimeFormat::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BMessage insert(fCommand);
		insert.AddString("format", Format(RadioValue(2)).c_str());
		insert.AddBool("static", CheckBoxItem(8));
		fWindow->PostMessage(&insert);
	}
	return TDialog::ItemHit(index, message);
}

TDialogWPFormula::TDialogWPFormula(BLooper* window, const std::string& formula,
	bool displayResult, bool displayFormula)
	:
	TDialog(300, 300, 'APPL', NULL),
	fWindow(window),
	fFormula(formula),
	fDisplayResult(displayResult),
	fDisplayFormula(displayFormula)
{
}


void
TDialogWPFormula::Init()
{
	if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
		ok->SetEnabled(!fFormula.empty());
	int32 radio = 4;
	if (fDisplayFormula)
		radio = fDisplayResult ? 6 : 5;
	SetRadioItem(radio, true);
	SetTextItem(2, fFormula.c_str());
	MakeFocusItem(2);
}


bool
TDialogWPFormula::ItemHit(int32 index, BMessage* message)
{
	if (index == 2) {
		// OK only with a formula
		if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
			ok->SetEnabled(TextItem(2).Length() > 0);
		return true;
	}
	if (index != 0)
		return TDialog::ItemHit(index, message);
	std::string formula = TextItem(2).String();
	if (formula.empty()) {
		(new BAlert("", GobeResources::IndexString(6, 4).String(), "OK"))->Go(NULL);
		MakeFocusItem(2);
		return true;
	}
	if (formula[0] != '=')
		formula.insert(0, "=");
	std::vector<FormulaToken> tokens;
	std::string error;
	if (!ParseFormula(formula.substr(1), CellID(), [](const std::string&) { return true; },
			tokens, error)) {
		(new BAlert("", error.c_str(), "OK"))->Go(NULL);
		return true;
	}
	int32 display = RadioValue(3);
	bool result = display == 0 || display == 2, shown = display == 1 || display == 2;
	BMessage insert('sM55');
	insert.AddString("formula", formula.c_str());
	insert.AddString("format", "General");
	insert.AddBool("displayFormula", shown);
	insert.AddBool("displayResult", result);
	fWindow->PostMessage(&insert);
	return TDialog::ItemHit(index, message);
}

TDialogSection::TDialogSection(BLooper* window, int32 columns)
	:
	TDialog(303, 303, 'APPL', NULL),
	fWindow(window),
	fColumns(columns)
{
}


void
TDialogSection::Init()
{
	BString text;
	text << fColumns;
	SetTextItem(2, text.String());
	MakeFocusItem(2);
}


bool
TDialogSection::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BString text = TextItem(2);
		char* end;
		long columns = strtol(text.String(), &end, 10);
		if (end == text.String() || columns < 1 || columns > 10) {
			BString alert;
			alert.SetToFormat(GobeResources::IndexString(6, 13).String(), 1, 10);
			(new BAlert("", alert.String(), "OK"))->Go(NULL);
			MakeFocusItem(2);
			return true;
		}
		BMessage set('sM51');
		set.AddInt32("value", int32(columns));
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

// the dialog's last choice (gIsEndnote)
static bool sIsEndnote = false;
static const uint32 kMsgMarkModified = 'MoDF';


TDialogWPInsertFootnote::TDialogWPInsertFootnote(BLooper* window, int16 first, bool mainText)
	:
	TDialog(306, 306, 'APPL', NULL),
	fWindow(window),
	fFirst(first),
	fMainText(mainText)
{
}


void
TDialogWPInsertFootnote::Init()
{
	SetRadioItem(4, !sIsEndnote);
	SetRadioItem(5, sIsEndnote);
	SetRadioItem(7, true);
	SetLabelItem(4, GobeResources::IndexString(0x516, fMainText ? 10 : 11).String());
	SetLabelItem(5, GobeResources::IndexString(0x516, fMainText ? 12 : 13).String());
	// typing a mark chooses Custom Mark
	if (BTextControl* mark = dynamic_cast<BTextControl*>(Item(9))) {
		BMessage* modified = new BMessage(kMsgMarkModified);
		modified->AddBool("isMod", true);
		mark->SetModificationMessage(modified);
	}
}


void
TDialogWPInsertFootnote::MessageReceived(BMessage* message)
{
	if (message->what == kMsgMarkModified) {
		SetRadioItem(7, false);
		SetRadioItem(8, true);
		if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
			ok->SetEnabled(TextItem(9).Length() > 0);
		return;
	}
	TDialog::MessageReceived(message);
}


bool
TDialogWPInsertFootnote::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
		{
			BMessage insert('sM5I');
			sIsEndnote = RadioValue(3) == 1;
			insert.AddBool("endnote", sIsEndnote);
			if (RadioValue(6) != 0) {
				BString mark = TextItem(9);
				if (mark.Length() == 0)
					return true;
				insert.AddString("mark", mark.String());
			}
			fWindow->PostMessage(&insert);
			break;
		}
		case 2:
			(new TDialogWPFootnoteOptions(fWindow, fFirst))->RunDialog();
			return true;
		case 7:
			MakeFocusItem(9);
			SetRadioItem(8, false);
			SetRadioItem(7, true);
			if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
				ok->SetEnabled(true);
			return true;
		case 8:
		{
			MakeFocusItem(9);
			BMessage modified(kMsgMarkModified);
			MessageReceived(&modified);
			return true;
		}
	}
	return TDialog::ItemHit(index, message);
}


TDialogWPFootnoteOptions::TDialogWPFootnoteOptions(BLooper* window, int16 first)
	:
	TDialog(307, 307, 'APPL', NULL),
	fWindow(window),
	fFirst(first)
{
}


void
TDialogWPFootnoteOptions::Init()
{
	BString text;
	text << fFirst;
	SetTextItem(2, text.String());
	MakeFocusItem(2);
}


bool
TDialogWPFootnoteOptions::ItemHit(int32 index, BMessage* message)
{
	if (index == 0) {
		BString text = TextItem(2);
		char* end;
		long first = strtol(text.String(), &end, 10);
		if (end == text.String())
			return true;
		BMessage set('sM5K');
		set.AddInt16("start", int16(first));
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

// a measure's unit "lines" (TMeasureUnit 5): 16 points each
static const int kLines = 5;


TDialogFormatParagraph::TDialogFormatParagraph(BLooper* window, const float values[6],
	const bool lines[3], int32 align, int unit, float width)
	:
	TDialog(304, 304, 'APPL', NULL),
	fWindow(window),
	fAlign(align),
	fWidth(width)
{
	// TDialogFormatParagraph::setStyle: the indents in the sheet's unit;
	// the spacings in lines (as points, 16 a line) or in the sheet's unit
	for (int i = 0; i < 3; i++)
		fValues[i] = {values[i], unit};
	for (int i = 3; i < 6; i++)
		fValues[i] = lines[i - 3] ? Value{values[i] * 16, kLines} : Value{values[i], unit};
	for (int32 i = 0; i < 6; i++)
		fUnitNames.push_back(GobeResources::IndexString(7, i).String());
}


TDialogFormatParagraph::Value&
TDialogFormatParagraph::ValueOf(int32 index)
{
	return fValues[index / 2 - 1];
}


void
TDialogFormatParagraph::ShowValue(int32 index)
{
	const Value& v = ValueOf(index);
	SetTextItem(index, MeasureText(v.points, v.unit, kMeasureWithUnit | kMeasureTwoPlaces,
		fUnitNames).c_str());
	// (the pop-ups list "lines" after a separator)
	SetPopUpItem(index + 1, v.unit);
}


void
TDialogFormatParagraph::Init()
{
	// no lines for the indents
	for (int32 popup = 3; popup < 8; popup += 2)
		RemoveLastPopUpItems(popup, 2);
	SetPopUpItem(14, fAlign);
	for (int32 index = 2; index <= 12; index += 2)
		ShowValue(index);
	MakeFocusItem(2);
}


// TDialogFormatParagraph::validTextControl: the text as a measure in its
// range (else STR# 6, 12 or 15)
bool
TDialogFormatParagraph::ValidTextControl(int32 index)
{
	float minimum = index == 8 ? 1 : 0, maximum = 160;
	if (index == 2 || index == 4)
		maximum = fWidth - ValueOf(6).points - 8;
	else if (index == 6)
		maximum = fWidth - ValueOf(2).points - 8;
	Value& v = ValueOf(index);
	float points;
	int unit = v.unit;
	if (!ParseMeasure(TextItem(index).String(), v.unit, fUnitNames, index >= 8, true, points, &unit)
			|| points < minimum || points > maximum) {
		BString alert;
		alert.SetToFormat(GobeResources::IndexString(6, 15).String(),
			MeasureText(minimum, v.unit, kMeasureWithUnit, fUnitNames).c_str(),
			MeasureText(maximum, v.unit, kMeasureWithUnit | kMeasureTwoPlaces,
				fUnitNames).c_str());
		(new BAlert("", alert.String(), "OK"))->Go(NULL);
		MakeFocusItem(index);
		return false;
	}
	v = {points, unit};
	ShowValue(index);
	return true;
}


bool
TDialogFormatParagraph::ValidTextControls()
{
	for (int32 index = 2; index <= 12; index += 2)
		if (!ValidTextControl(index))
			return false;
	return true;
}


bool
TDialogFormatParagraph::ItemHit(int32 index, BMessage* message)
{
	int32 value = 0;
	if (message != NULL)
		message->FindInt32(kValueString, &value);
	if (index == 0) {
		if (!ValidTextControls())
			return true;
		BMessage format('sM50');
		// items 2 left, 4 first line, 6 right, 8 line spacing, 10 before,
		// 12 after; a spacing in lines as lines (TWPPart::DoParagraphFormat)
		static const char* const kNames[6] = {"left", "first", "right", "lineSpacing",
			"spaceBefore", "spaceAfter"};
		for (int i = 0; i < 6; i++) {
			bool lines = i >= 3 && fValues[i].unit == kLines;
			format.AddFloat(kNames[i], lines ? fValues[i].points * 0.0625f : fValues[i].points);
			if (i >= 3)
				format.AddBool((std::string(kNames[i]) + "Lines").c_str(), lines);
		}
		format.AddInt32("align", fAlign);
		fWindow->PostMessage(&format);
		return TDialog::ItemHit(index, message);
	}
	if (index >= 2 && index <= 12 && index % 2 == 0) {
		ValidTextControl(index);
		return true;
	}
	if (index >= 3 && index <= 13) {
		// a unit chosen: the value shown in it
		ValueOf(index - 1).unit = value;
		ShowValue(index - 1);
		return true;
	}
	if (index == 14) {
		fAlign = value;
		return true;
	}
	return TDialog::ItemHit(index, message);
}

TDialogGRSize::TDialogGRSize(BLooper* window, uint32 command, const char* title,
	const char* label, const char* text)
	:
	TDialog(31, 31, 'APPL', NULL),
	fWindow(window),
	fCommand(command),
	fTitle(title),
	fLabel(label),
	fText(text)
{
}


void
TDialogGRSize::Init()
{
	SetTitle(fTitle.String());
	SetLabelItem(2, fLabel.String());
	SetTextItem(2, fText.String());
	MakeFocusItem(2);
	if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
		ok->SetEnabled(fText.Length() > 0);
}


bool
TDialogGRSize::ItemHit(int32 index, BMessage* message)
{
	if (index == 2) {
		if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
			ok->SetEnabled(TextItem(2).Length() > 0);
		return true;
	}
	if (index == 0) {
		BString text = TextItem(2);
		char* end;
		float size = strtof(text.String(), &end);
		if (end == text.String() || size < 2 || size > 510) {
			BString alert;
			alert.SetToFormat(GobeResources::IndexString(6, 13).String(), 2, 510);
			(new BAlert("", alert.String(), "OK"))->Go(NULL);
			MakeFocusItem(2);
			return true;
		}
		BMessage set(fCommand);
		set.AddFloat(kValueString, size);
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

TWordCountDialog::TWordCountDialog(const WordCount& count, bool selection)
	:
	TDialog(54, 54, 'APPL', NULL),
	fCount(count),
	fSelection(selection)
{
}


void
TWordCountDialog::Init()
{
	BString text;
	text << fCount.characters;
	SetTextItem(5, text.String());
	text = "";
	text << fCount.words;
	SetTextItem(6, text.String());
	if (fSelection) {
		for (int32 index : {3, 7, 4, 8})
			if (Item(index) != NULL)
				Item(index)->Hide();
		return;
	}
	text = "";
	text << fCount.paragraphs;
	SetTextItem(7, text.String());
	text = "";
	text << fCount.pages;
	SetTextItem(8, text.String());
}



TDialogTabEdit::TDialogTabEdit(BLooper* window, BHandler* ruler, int32 index,
	const TabStop& tab, int unit, float start, float end)
	:
	TDialog(305, 305, 'APPL', NULL),
	fWindow(window),
	fRuler(ruler),
	fIndex(index),
	fTab(tab),
	fUnit(unit),
	fStart(start),
	fEnd(end)
{
	fTab.position += start;
	for (int32 i = 0; i < 6; i++)
		fUnitNames.push_back(GobeResources::IndexString(7, i).String());
}


static std::string
CharText(uint32 c)
{
	// (the characters are stored as their UTF-8 bytes, first byte lowest)
	std::string text;
	for (int i = 0; i < 4 && (c >> (8 * i)) != 0; i++)
		text += char((c >> (8 * i)) & 0xff);
	return text;
}


void
TDialogTabEdit::Init()
{
	for (int32 i = 3; i <= 6; i++)
		SetRadioItem(i, i == 3 + std::min(std::max(fTab.type, 0), 3));
	SetTextItem(7, CharText(fTab.alignChar).c_str());
	for (int32 i = 9; i <= 11; i++)
		SetRadioItem(i, i == 9 + std::min(std::max(fTab.fill, 0), 2));
	SetTextItem(12, CharText(fTab.fillChar).c_str());
	SetTextItem(13, MeasureText(fTab.position, fUnit, kMeasureWithUnit | kMeasureTwoPlaces,
		fUnitNames).c_str());
}


// items 7 and 12: the first character typed, if any
void
TDialogTabEdit::ReadChar(int32 index, uint32& c)
{
	BString text = TextItem(index);
	if (text.Length() == 0)
		return;
	const unsigned char* bytes = (const unsigned char*)text.String();
	int length = bytes[0] < 0x80 ? 1 : bytes[0] < 0xe0 ? 2 : bytes[0] < 0xf0 ? 3 : 4;
	c = 0;
	for (int i = 0; i < length && i < text.Length(); i++)
		c |= uint32(bytes[i]) << (8 * i);
}


// item 13: a measure from the text's left to its right edge
bool
TDialogTabEdit::ValidPosition()
{
	float points;
	int unit = fUnit;
	if (!ParseMeasure(TextItem(13).String(), fUnit, fUnitNames, false, false, points, &unit)
			|| points < fStart || fEnd < points) {
		BString alert;
		alert.SetToFormat(GobeResources::IndexString(6, 15).String(),
			MeasureText(fStart, fUnit, kMeasureTwoPlaces, fUnitNames).c_str(),
			MeasureText(fEnd, fUnit, kMeasureWithUnit, fUnitNames).c_str());
		(new BAlert("", alert.String(), "OK"))->Go(NULL);
		MakeFocusItem(13);
		return false;
	}
	fUnit = unit;
	fTab.position = points;
	SetTextItem(13, MeasureText(points, fUnit, kMeasureWithUnit, fUnitNames).c_str());
	return true;
}


bool
TDialogTabEdit::ItemHit(int32 index, BMessage* message)
{
	switch (index) {
		case 0:
		{
			// (the port checks the fields again: OK may come before their
			// changes)
			if (!ValidPosition())
				return true;
			ReadChar(7, fTab.alignChar);
			ReadChar(12, fTab.fillChar);
			BMessage modify(kMsgModifyTab);
			modify.AddInt32("tab", fIndex);
			modify.AddFloat("position", fTab.position - fStart);
			modify.AddInt32("type", fTab.type);
			modify.AddInt32("alignChar", int32(fTab.alignChar));
			modify.AddInt32("fill", fTab.fill);
			modify.AddInt32("fillChar", int32(fTab.fillChar));
			fWindow->PostMessage(&modify, fRuler);
			break;
		}
		case 3: case 4: case 5: case 6:
			for (int32 i = 3; i <= 6; i++)
				SetRadioItem(i, i == index);
			fTab.type = index - 3;
			return true;
		case 7:
			ReadChar(7, fTab.alignChar);
			return true;
		case 9: case 10: case 11:
			for (int32 i = 9; i <= 11; i++)
				SetRadioItem(i, i == index);
			fTab.fill = index - 9;
			return true;
		case 12:
			ReadChar(12, fTab.fillChar);
			return true;
		case 13:
			ValidPosition();
			return true;
	}
	return TDialog::ItemHit(index, message);
}


TDialogSetRulerDivisions::TDialogSetRulerDivisions(BLooper* window, const char* title,
	const char* label, const char* text)
	:
	TDialog(31, 31, 'APPL', NULL),
	fWindow(window),
	fTitle(title),
	fLabel(label),
	fText(text)
{
}


// TDialogGenericGetString::Init
void
TDialogSetRulerDivisions::Init()
{
	SetTitle(fTitle.String());
	SetLabelItem(2, fLabel.String());
	SetTextItem(2, fText.String());
	MakeFocusItem(2);
}


// TDialogGenericGetString::ItemHit, TDialogSetRulerDivisions::
// ValidateAndSendMsg
bool
TDialogSetRulerDivisions::ItemHit(int32 index, BMessage* message)
{
	if (index == 2) {
		if (BButton* ok = dynamic_cast<BButton*>(Item(0)))
			ok->SetEnabled(TextItem(2).Length() > 0);
		return true;
	}
	if (index == 0) {
		BString text = TextItem(2);
		char* end;
		long divisions = strtol(text.String(), &end, 10);
		if (end == text.String() || *end != '\0' || divisions < 1 || divisions > 512) {
			BString alert;
			alert.SetToFormat(GobeResources::IndexString(6, 13).String(), 1, 512);
			(new BAlert("", alert.String(), "OK"))->Go(NULL);
			MakeFocusItem(2);
			return true;
		}
		BMessage set('s00L');
		set.AddInt32("rulerDivisions", int32(divisions));
		fWindow->PostMessage(&set);
	}
	return TDialog::ItemHit(index, message);
}

} // namespace gobe
