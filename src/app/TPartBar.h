// TPartBar: the ribbon of the active part's tools (TPartBar, with
// TWPPart::AddPartBarElements and PartBarUpdateContents for text): the
// part's icon; the alignments ('sM5B', sticky); line spacing less, its
// value (TSpacingEdit: click to type lines or a length, 1 point to 10
// lines) and more ('sM5D'); a column less and more ('sM51', not in a part
// of kind 2); Check Spelling ('sMC5'); Show/Hide Invisibles ('sM5J').
// For cells (TPartSS::AddPartBarElements): the cell address (a double
// click: Go To Cell…), the horizontal and vertical alignments (MENU 302 and
// 303, 'sM67'), the borders (not ported: off), Number Formatting ('sM60')
// and Make Chart (not ported: off).
#ifndef GOBE_TPARTBAR_H
#define GOBE_TPARTBAR_H

#include "TRibbon.h"

namespace gobe {

class TPageView;
class TSpacingEdit;

class TPartBar : public TRibbon {
public:
	TPartBar(TPageView* pages);

	virtual void Update();

	// the line spacing as the field shows it: a value in points or lines
	void SpacingEntered(float value, bool lines);
	void SpacingCancelled();
	// test hook: "spacing" typed into the line spacing and Enter
	void TypeSpacing(const char* text);

private:
	void Build(bool text, uint16 kind);
	// TPartSS::AddPartBarElements
	void BuildSheet();
	// TPartSS::PartBarUpdateContents
	void UpdateSheet();

	TPageView* fPages;
	bool fSheet = false;
	TSpacingEdit* fSpacing = NULL;
	bool fText = false;
	uint16 fKind = 0;
	// what Update last showed
	int32 fAlignment = -2;
	float fLineSpacing = -1;
	bool fLines = false;
	int fUnit = -1;
	int32 fColumns = -1;
	bool fColumnsOn = false;
};

} // namespace gobe

#endif
