// A shape's own dialogs, opened from its context menu: an arc's Start
// Angle… and Arc Angle… (TDialogAngle, DIAL/Item 30) and a round
// rectangle's Corner Radius… (TDialogRadius, DIAL/Item 201); and Document
// Size… for drawings (TDialogGRDocSize, DIAL/Item 200) and presentations
// (TDialogPRDocSize, DIAL/Item 11005 of Parts/Presentation).
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

// TDialogGRDocSize: the pages across (2) and down (3), each 1 to 40 (else
// STR# 6, 13); OK sends the window 'sM21' with floats "Width" and "Height".
class TDialogGRDocSize : public TDialog {
public:
	TDialogGRDocSize(BLooper* window, float across, float down);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	float fPages[2];
};

// TDialogPRDocSize: a pop-up of slide sizes (2; STR# 11002, 3 "%d × %d",
// the one matching marked) that fills in the width (3) and height (4), each
// 1 to 10000 (else STR# 6, 13); OK sends the window 'sM21' with int32
// "Width" and "Height".
class TDialogPRDocSize : public TDialog {
public:
	TDialogPRDocSize(BLooper* window, int32 width, int32 height);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	int32 fSize[2];
};

} // namespace gobe

#endif
