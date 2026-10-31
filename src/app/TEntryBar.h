// TEntryBar: the spreadsheet's ribbon (TPartSS::ActivePartRibbon) under the
// PartBar while cells are selected: Accept (MICN 40) and Cancel (41),
// on while editing; the Paste Function button (42; its dialog isn't
// ported: off); and the active cell's contents to edit (TEntryEdit).
// Enter or Tab there send 's007' with "key", "modifiers" and "text" to the
// window, Escape 's008'; typing on a selected cell begins editing here.
#ifndef GOBE_TENTRYBAR_H
#define GOBE_TENTRYBAR_H

#include "TRibbon.h"

namespace gobe {

class TEntryEdit;
class TPageView;

class TEntryBar : public TRibbon {
public:
	TEntryBar(TPageView* pages);

	virtual void Update();

	// TEntryBar::BeginEdit: the edit focused, the buttons on; `bytes`
	// (a key typed on the cell) replaces what it holds
	void BeginEdit(const char* bytes = NULL);
	// TEntryBar::EndEdit: the buttons off, the cell's contents shown again
	void EndEdit();
	bool InEdit() const { return fEditing; }
	const char* Text() const;
	// the edit's text (a test hook types into it)
	void SetText(const char* text);

	// a key for the edit (the page view passes on keys it gets while
	// editing goes on here, as a test hook's do)
	void Key(const char* bytes, int32 count);

	// Accept and Cancel (TAcceptButton, TCancelButton)
	void Accept(int32 key, int32 modifiers);
	void Cancel();

private:
	TPageView* fPages;
	TEntryEdit* fEdit;
	bool fEditing = false;
	std::string fShown;		// the cell's contents Update last showed
};

} // namespace gobe

#endif
