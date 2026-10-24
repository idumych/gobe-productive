// Help > About Gobe Productive… (TApp::AboutRequested, TDialogSplash,
// DIAL 19): the logo (BMPm "GPlogo"), the version (APPV) and the copyright
// (STR# 5, 44), and OK. (The credits the original scrolls over the logo,
// STR# 15 with the flags, aren't ported.)
#ifndef GOBE_TDIALOG_SPLASH_H
#define GOBE_TDIALOG_SPLASH_H

#include <Window.h>

namespace gobe {

class TDialogSplash : public BWindow {
public:
	TDialogSplash();

	virtual void MessageReceived(BMessage* message);
};

} // namespace gobe

#endif
