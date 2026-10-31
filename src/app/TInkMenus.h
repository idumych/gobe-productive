// The ink menus the ribbons share (TInkSetMenu, TMICNPickView): an ink
// set's colors as a matrix of cells, and a menu item showing a MICN with
// its set's menu behind it.
#ifndef GOBE_TINKMENUS_H
#define GOBE_TINKMENUS_H

#include <Bitmap.h>
#include <Menu.h>
#include <MenuItem.h>

#include <memory>
#include <vector>

namespace gobe {

class ResourceFile;

// the inks of a COLR resource (TInkSet::LoadColorSetResource): columns,
// cell size, gap, then RGB as three u32s each
struct ColorSet {
	int32 columns = 1, cell = 10, gap = 2;
	std::vector<rgb_color> colors;
};
ColorSet LoadColorSet(int32 id);

// TInkSetMenu: the active color set (COLR 0) as a matrix; each cell sends a
// copy of `model` with "ink name", "color" (B_RGB_COLOR_TYPE) and "ink index"
BMenu* ColorSetMenu(const BMessage& model);

// TInkMenuItem: a cell of an ink set's matrix menu
class TInkMenuItem : public BMenuItem {
public:
	TInkMenuItem(rgb_color color, int32 cell, BMessage* message);

	virtual void DrawContent();
	virtual void GetContentSize(float* width, float* height);

private:
	rgb_color fColor;
	int32 fCell;
};

// TMICNPickView's icons: an item showing a MICN (the application's, or
// `resources`), with its set's menu
class TIconMenuItem : public BMenuItem {
public:
	TIconMenuItem(int32 micn, BMenu* menu, const ResourceFile* resources = NULL);

	virtual void DrawContent();
	virtual void GetContentSize(float* width, float* height);

private:
	std::unique_ptr<BBitmap> fIcon;
};

} // namespace gobe

#endif
