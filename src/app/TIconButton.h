// TIconButton: a dialog's button showing a 32 x 32 icon (ICON) in a 36 x 36
// frame (TIconButton::calcFrameSize). The buttons of one selection set
// under the same parent act as radio buttons: one turned on turns the
// others off (TIconButton::SetValue). A button that isn't visible draws
// only its background (TIconButton::SetVisible).
#ifndef GOBE_TICON_BUTTON_H
#define GOBE_TICON_BUTTON_H

#include <Control.h>

namespace gobe {

class ResourceFile;

class TIconButton : public BControl {
public:
	// the icon `id` of `resources` (else the application's); `set` 0: in
	// no selection set
	TIconButton(BMessage* message, int32 id, int32 set, bool visible,
		const ResourceFile* resources);
	virtual ~TIconButton();

	// TIconButton::LoadNewImage: icon `id` shown instead
	void LoadNewImage(int32 id);
	// TIconButton::SetVisible
	void SetVisible(bool visible);
	bool IsVisible() const { return fVisible; }

	virtual void SetValue(int32 value);
	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage);
	virtual void MouseUp(BPoint where);

private:
	const ResourceFile* fResources;
	int32 fSet;
	bool fVisible;
	bool fPressed = false;
	BBitmap* fIcon = NULL;
};

} // namespace gobe

#endif
