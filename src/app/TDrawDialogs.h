// A shape's own dialogs, opened from its context menu: an arc's Start
// Angle… and Arc Angle… (TDialogAngle, DIAL/Item 30) and a round
// rectangle's Corner Radius… (TDialogRadius, DIAL/Item 201).
#ifndef GOBE_TDRAW_DIALOGS_H
#define GOBE_TDRAW_DIALOGS_H

#include "TDialog.h"

namespace gobe {

// TDialogAngle: the angle (2), 0 to 360; OK sends the window 'sM4P' with
// "Type" (1 the start angle, 2 the arc angle) and "Angle". The title is
// STR# 1101, 18 or 19 (Set Start Angle, Set Arc Angle).
class TDialogAngle : public TDialog {
public:
	TDialogAngle(BLooper* window, int32 type, float angle);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	int32 fType;
	float fAngle;
};

// TDialogRadius: the horizontal (2) and vertical (3) radius, each 0 or
// more; OK sends the window 'sM4R' with "XRadius" and "YRadius".
class TDialogRadius : public TDialog {
public:
	TDialogRadius(BLooper* window, float x, float y);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	float fRadius[2];
};

} // namespace gobe

#endif
