// The image processing part's dialogs (Parts/ImageProcessing).
#ifndef GOBE_TIMAGE_DIALOGS_H
#define GOBE_TIMAGE_DIALOGS_H

#include <string>
#include <vector>

#include "TDialog.h"

namespace gobe {

// TDialogBTDocSize (DIAL 1 of the image part): Document Size… for an
// image, its width and height in points ("512 pt"); OK sends 'sM21' with
// "Width" and "Height" (each 1 to 10000 points, else SimpleAlert(6, 4))
class TDialogBTDocSize : public TDialog {
public:
	TDialogBTDocSize(BLooper* window, float width, float height);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	float fSize[2];
	std::vector<std::string> fUnitNames;
};

} // namespace gobe

#endif
