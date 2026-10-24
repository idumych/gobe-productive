// Document dialogs: Margins (TDialogFormatDocument).
#ifndef GOBE_TDOCUMENT_DIALOGS_H
#define GOBE_TDOCUMENT_DIALOGS_H

#include <string>
#include <vector>

#include "TDialog.h"

class BLooper;

namespace gobe {

struct Document;

// Document > Margins… (DIAL/Item 20, "Document Margins"): the first
// sheet's margins in its unit and "Show Page Guides"; OK sends the window
// 'sM22' with "Margins" (left, top, right, bottom), "MirrorFacingPages",
// "ShowMargins" and "ShowPageGuides".
class TDialogFormatDocument : public TDialog {
public:
	TDialogFormatDocument(BLooper* window, const Document& document);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	bool ValidTextControl(int32 index, float& value);
	float Maximum(int32 index) const;

	BLooper* fWindow;
	float fMargins[4];		// left, top, right, bottom
	float fPaper[4];
	int fUnit;
	bool fMirror, fShowMargins, fShowGuides;
	std::vector<std::string> fUnitNames;
};

} // namespace gobe

#endif
