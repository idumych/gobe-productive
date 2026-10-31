// TFormatBar: the ribbon of text and ink formatting (TFormatBar::
// LoadAllElements, UpdateContents): the font (TFontNameMenuElement), size
// (TFontSizeMenuElement, MENU 101), face (TFontFaceMenuElement, MENU 102),
// text ink (TTextMenuElement: color, pattern and gradient sets and MENU
// 103's transparency) with its indicator, fill and pen inks with theirs,
// shape transparency, styles (TStylesMenuElement, MENU 100 and the
// document's named styles) and named selections (TNamesMenuElement, MENU
// 111). The labels show what the selection has ("Font", "Size", "Styles"
// when it differs).
#ifndef GOBE_TFORMATBAR_H
#define GOBE_TFORMATBAR_H

#include "TRibbon.h"

namespace gobe {

// the style messages' fields (kStyleNameStr, kStyleLevelStr, kStyleTypeStr)
static const char* const kStyleNameStr = "Style Name";
static const char* const kStyleLevelStr = "Style Level";
static const char* const kStyleTypeStr = "Style Type";

class TPageView;
class TFormatMenuElement;
class TInkIndicatorElement;

class TFormatBar : public TRibbon {
public:
	TFormatBar(TPageView* pages);

	virtual void Draw(BRect updateRect);
	virtual void Update();

	TPageView* Pages() const { return fPages; }

	// test hook: the labels shown ("font", "size", "style") or the text
	// indicator's color ("ink")
	std::string Shown(const std::string& what) const;
	// test hook: what choosing color `index` of the text (fill) ink's color set,
	// or style `name` in Styles, sends (handled at once)
	void ChooseColor(int32 index, bool fill = false);
	void ChooseStyle(const char* name);

private:
	TPageView* fPages;
	TFormatMenuElement* fFont;
	TFormatMenuElement* fSize;
	TFormatMenuElement* fStyles;
	TInkIndicatorElement* fTextInk;
	TInkIndicatorElement* fFillInk;
	TInkIndicatorElement* fPenInk;
};

} // namespace gobe

#endif
