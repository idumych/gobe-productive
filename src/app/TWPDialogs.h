// Word processing dialogs: Insert Date/Time (TDialogWPDateTimeFormat).
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

} // namespace gobe

#endif
