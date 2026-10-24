// Word processing dialogs: Insert Date/Time (TDialogWPDateTimeFormat),
// Insert Formula (TDialogWPFormula), Columns (TDialogSection).
#ifndef GOBE_TWP_DIALOGS_H
#define GOBE_TWP_DIALOGS_H

#include <string>

#include "TDialog.h"

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

} // namespace gobe

#endif
