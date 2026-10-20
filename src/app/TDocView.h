// TDocView: shows a loaded document. For now: the word processing parts'
// text, one after another, with each style run's font, size, face and color.
// The original lays parts out in frames on pages (TPagePart/TRootPart and
// portals); that comes with the frame/portal port.
#ifndef GOBE_TDOCVIEW_H
#define GOBE_TDOCVIEW_H

#include <Font.h>
#include <View.h>

#include "Document.h"
#include "Styles.h"

class BTextView;

namespace gobe {

class TDocView : public BView {
public:
	TDocView(const Document& document);

	// Gobe's fonts (BeOS-era Bitstream) mapped to the closest Haiku family
	static BFont MapFont(const TextStyle& style);

private:
	BTextView* fText;
};

} // namespace gobe

#endif
