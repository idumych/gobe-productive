// The image processing part's dialogs (Parts/ImageProcessing).
#ifndef GOBE_TIMAGE_DIALOGS_H
#define GOBE_TIMAGE_DIALOGS_H

#include <string>
#include <vector>

#include "ImageTools.h"
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

// a tool's settings (IMToolAddOn's message, from GetFactoryConfig: kept
// while the application runs, for every image): the bucket's, magic
// wand's and Find Color's "tolerance" (percent), the airbrush's
struct ImageToolConfig {
	float tolerance = 20;
	AirbrushSettings airbrush;
};
// by TPartIMFactory's index
ImageToolConfig& ToolConfig(int32 tool);

// TDialogConfigTool (DIAL 4 of the image part): the tool's settings view
// (GetConfigurationView: ToleranceConfigView, Item 5; AirbrushConfigView,
// Item 6) in its USER item, titled with the tool's name, at the pointer;
// the sliders change the settings as they're let go, Cancel puts them back
class TDialogConfigTool : public TDialog {
public:
	explicit TDialogConfigTool(int32 tool);

	// IsValid: the tool has a settings view
	static bool HasSettings(int32 tool);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);
	virtual void PlaceWindow();

private:
	int32 fTool;
	ImageToolConfig fSaved;
};

} // namespace gobe

#endif
