#include "TInkEditors.h"

#include <Alert.h>
#include <Bitmap.h>
#include <ColorControl.h>
#include <Control.h>
#include <Gradient.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <GroupLayout.h>
#include <LayoutItem.h>
#include <Looper.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <Window.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "GobeResources.h"
#include "TDialog.h"
#include "TDocWindow.h"
#include "TInkMenus.h"
#include "TPageView.h"
#include "TRibbon.h"

namespace gobe {

namespace {

const uint32 kMsgScript = 'GieS';

rgb_color
ToRGB(const Color& c)
{
	return make_color(c.r, c.g, c.b, 255);
}


Color
FromRGB(rgb_color c)
{
	return Color{c.red, c.green, c.blue, 255};
}


// a STR# 8 string less its "…" (TDialogNewColor's titles)
BString
TitleString(int32 index)
{
	BString title = GobeResources::IndexString(8, index);
	title.TruncateChars(std::max(int32(0), title.CountChars() - 1));
	return title;
}


// a view in a USER item, filling it (TUserItemEngineer's procs)
void
FillUser(BView* user, BView* view)
{
	if (user == NULL)
		return;
	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 0);
	user->SetLayout(layout);
	layout->AddView(view);
}


// the group layout holding `view` among `layout`'s (nested groups are
// layout items, not views)
BGroupLayout*
GroupOf(BLayout* layout, BView* view)
{
	if (layout == NULL)
		return NULL;
	for (int32 i = 0; BLayoutItem* item = layout->ItemAt(i); i++) {
		if (item->View() == view)
			return dynamic_cast<BGroupLayout*>(layout);
		if (BLayout* inner = dynamic_cast<BLayout*>(item))
			if (BGroupLayout* found = GroupOf(inner, view))
				return found;
	}
	return NULL;
}


// a control TDialog doesn't build (PIKR, 256P, an IBUT of a MICN) put where
// its placeholder is; the placeholder hidden
void
PutInPlace(BView* placeholder, BView* view)
{
	BView* parent = placeholder != NULL ? placeholder->Parent() : NULL;
	BGroupLayout* group = parent != NULL ? GroupOf(parent->GetLayout(), placeholder) : NULL;
	if (group == NULL)
		return;
	group->AddView(group->IndexOfView(placeholder) + 1, view);
	placeholder->Hide();
}


// the message a control of dialog `dialog` sends for item `index`, as
// TDialog's (ItemHit gets it)
BMessage*
ItemMessage(int32 dialog, int32 index)
{
	BMessage* message = new BMessage('APPL' + index);
	message->AddInt32("dialog", dialog);
	message->AddInt32("index", index);
	return message;
}


// an ink drawn in `r` (as TPageView::SetInk draws it)
void
FillInk(BView* view, BRect r, const Ink& ink)
{
	if (ink.gradient) {
		BGradient* g;
		if (ink.gradientType == Sig("GDrl")) {
			BPoint center(r.left + r.Width() * ink.center[0], r.top + r.Height() * ink.center[1]);
			g = new BGradientRadial(center, std::max(r.Width(), r.Height()) / 2);
		} else {
			float a = ink.angle * M_PI / 180;
			BPoint c(r.left + r.Width() / 2, r.top + r.Height() / 2);
			BPoint d(cosf(a) * r.Width() / 2, -sinf(a) * r.Height() / 2);
			g = new BGradientLinear(c - d, c + d);
		}
		int count = std::max(2, std::min(4, ink.colorCount));
		for (int i = 0; i < count; i++)
			g->AddColor(ToRGB(ink.colors[i]), 255 * i / (count - 1));
		view->FillRect(r, *g);
		delete g;
		return;
	}
	pattern p;
	for (int i = 0; i < 8; i++)
		p.data[i] = ink.pattern[i];
	rgb_color low = view->LowColor();
	view->SetHighColor(ToRGB(ink.color));
	view->SetLowColor(ToRGB(ink.low));
	view->FillRect(r, p);
	view->SetLowColor(low);
}


// the editor open (one at a time: they're modal)
class TInkEditor;
TInkEditor* sEditor = NULL;


// TDialogNewColor's editors: OK posts the ink
class TInkEditor : public TDialog {
public:
	TInkEditor(int32 dialog, BLooper* target, const BMessage& reply)
		:
		TDialog(dialog, dialog, 'APPL', NULL),
		fTarget(target),
		fReply(reply),
		fNew(reply.what == 'sfI0')
	{
		sEditor = this;
	}

	virtual ~TInkEditor()
	{
		if (sEditor == this)
			sEditor = NULL;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgScript) {
			Script(message->GetString("command", ""));
			return;
		}
		TDialog::MessageReceived(message);
	}

	virtual void Script(const std::string& command) = 0;

protected:
	// the ink posted (TPenInk::Archive into a copy of the reply)
	void Post(const Ink& ink, const rgb_color* color = NULL)
	{
		if (fTarget == NULL)
			return;
		BMessage reply(fReply);
		std::vector<uint8_t> bytes = InkBytes(ink);
		reply.AddData("ink", B_RAW_TYPE, bytes.data(), ssize_t(bytes.size()));
		if (color != NULL)
			reply.AddData("color", B_RGB_COLOR_TYPE, color, sizeof(rgb_color));
		fTarget->PostMessage(&reply);
	}

	void Close()
	{
		if (sEditor == this)
			sEditor = NULL;
		PostMessage(B_QUIT_REQUESTED);
	}

	BLooper* fTarget;
	BMessage fReply;
	bool fNew;
};


// a script's numbers
std::vector<float>
Numbers(const std::string& text)
{
	std::vector<float> numbers;
	const char* p = text.c_str();
	char* end;
	while (*p != '\0') {
		float value = strtof(p, &end);
		if (end == p) {
			p++;
			continue;
		}
		numbers.push_back(value);
		p = end;
	}
	return numbers;
}


// TColorPicker (PIKR): a BColorControl sending its item
BColorControl*
MakePicker(int32 dialog, int32 index, rgb_color value)
{
	BColorControl* picker = new BColorControl(B_ORIGIN, B_CELLS_32x8, 6, "picker",
		ItemMessage(dialog, index));
	picker->SetValue(value);
	return picker;
}


// ---- TDialogEditColor (DIAL 6): OK 0, Cancel 1, the sample 2 (the
// original colour above, the new one below; a click above goes back to the
// original), the picker 3

class TDialogEditColor;

class TColorSampleView : public BView {
public:
	TColorSampleView(TDialogEditColor* dialog)
		:
		BView("sample", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fDialog(dialog)
	{
	}

	virtual void Draw(BRect);
	virtual void MouseDown(BPoint where);

private:
	TDialogEditColor* fDialog;
};


class TDialogEditColor : public TInkEditor {
public:
	TDialogEditColor(rgb_color color, BLooper* target, const BMessage& reply)
		:
		TInkEditor(6, target, reply),
		fOriginal(color),
		fColor(color)
	{
	}

	rgb_color OriginalColor() const { return fOriginal; }
	rgb_color NewColor() const { return fColor; }

	void SetColor(rgb_color color)
	{
		fColor = color;
		if (fPicker != NULL)
			fPicker->SetValue(color);
		if (fSample != NULL)
			fSample->Invalidate();
	}

protected:
	// TDialogEditColor::Init: the title (New Color or Edit Color), the
	// picker on the colour
	virtual void Init()
	{
		SetTitle(TitleString(fNew ? 0 : 8).String());
		fSample = new TColorSampleView(this);
		FillUser(Item(2), fSample);
		fPicker = MakePicker(6, 3, fColor);
		PutInPlace(Item(3), fPicker);
	}

	virtual bool ItemHit(int32 index, BMessage*)
	{
		switch (index) {
			case 0:
			{
				// TPenInk(TColor) archived, and "color"
				Ink ink;
				ink.set = true;
				ink.color = FromRGB(fColor);
				ink.low = Color{255, 255, 255, 255};
				Post(ink, &fColor);
				Close();
				return false;
			}
			case 1:
				Close();
				return false;
			case 2:
				SetColor(fOriginal);
				return true;
			case 3:
				fColor = fPicker->ValueAsColor();
				fSample->Invalidate();
				return true;
		}
		return true;
	}

	// a colour pasted ('PSTE' with "RGBColor")
	virtual void MessageReceived(BMessage* message)
	{
		const void* data;
		ssize_t size;
		if (message->what == 'PSTE'
				&& message->FindData("RGBColor", B_RGB_COLOR_TYPE, &data, &size) == B_OK
				&& size >= 4) {
			SetColor(*(const rgb_color*)data);
			return;
		}
		TInkEditor::MessageReceived(message);
	}

	virtual void Script(const std::string& command)
	{
		std::vector<float> n = Numbers(command);
		if (command.compare(0, 6, "color ") == 0 && n.size() == 3) {
			fPicker->SetValue(make_color(uint8(n[0]), uint8(n[1]), uint8(n[2]), 255));
			BMessage hit('APPL' + 3);
			ItemHit(3, &hit);
		} else if (command == "ok" || command == "cancel" || command == "revert") {
			BMessage hit('APPL');
			ItemHit(command == "ok" ? 0 : command == "cancel" ? 1 : 2, &hit);
		} else if (command == "show") {
			printf("inkeditor color \"%s\" original %d %d %d now %d %d %d\n", Title(),
				fOriginal.red, fOriginal.green, fOriginal.blue, fColor.red, fColor.green,
				fColor.blue);
			fflush(stdout);
		}
	}

private:
	rgb_color fOriginal;
	rgb_color fColor;
	BColorControl* fPicker = NULL;
	TColorSampleView* fSample = NULL;
};


void
TColorSampleView::Draw(BRect)
{
	BRect r = Bounds();
	float middle = std::floor((r.top + r.bottom) / 2);
	SetHighColor(fDialog->OriginalColor());
	FillRect(BRect(r.left, r.top, r.right, middle));
	SetHighColor(fDialog->NewColor());
	FillRect(BRect(r.left, middle + 1, r.right, r.bottom));
	SetHighColor(0, 0, 0);
	StrokeRect(r);
}


void
TColorSampleView::MouseDown(BPoint where)
{
	BRect r = Bounds();
	if (where.y < (r.top + r.bottom) / 2) {
		std::unique_ptr<BMessage> hit(ItemMessage(6, 2));
		Window()->PostMessage(hit.get());
	}
}


// an icon button (TIconButton of a MICN) that stays down while its tool is
// chosen
class TToolButton : public BControl {
public:
	TToolButton(int32 micn, BMessage* message)
		:
		BControl("tool", NULL, message, B_WILL_DRAW),
		fIcon(TMICNElement::LoadIcon(micn))
	{
		SetExplicitMinSize(BSize(23, 23));
		SetExplicitMaxSize(BSize(23, 23));
	}

	virtual void Draw(BRect)
	{
		BRect r = Bounds();
		if (Value() == B_CONTROL_ON)
			TRibbon::DrawOutlineDown(this, r);
		else {
			SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
			FillRect(r);
		}
		if (fIcon != NULL) {
			SetDrawingMode(B_OP_OVER);
			DrawBitmap(fIcon.get(), BPoint(r.left + 3, r.top + 3));
			SetDrawingMode(B_OP_COPY);
		}
	}

	virtual void MouseDown(BPoint)
	{
		if (IsEnabled())
			Invoke();
	}

private:
	std::unique_ptr<BBitmap> fIcon;
};


// ---- TDialogEditPattern (DIAL 3): the edit view 0 (291 x 291), the
// picker 1, the sample 2, Width 3 and Height 4 (MENU 244: 8, 16, 32, 64),
// the tools 5 (pencil: the picker's colour; with the secondary button
// white), 6 (the eyedropper: a pixel's colour to the picker), 7 (every
// pixel of the clicked one's colour in the picker's), OK 8, Cancel 9,
// Revert 10 (once changed)

class TDialogEditPattern;

class TPatternEditView : public BView {
public:
	TPatternEditView(TDialogEditPattern* dialog, bool sample)
		:
		BView(sample ? "sample" : "edit", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fDialog(dialog),
		fSample(sample)
	{
	}

	virtual void Draw(BRect);
	virtual void MouseDown(BPoint where);
	virtual void MouseMoved(BPoint where, uint32 transit, const BMessage* drag);
	virtual void MouseUp(BPoint)
	{
		fTracking = false;
	}

	// the cell under `where` (TDialogEditPattern::GetRowCol); false outside
	bool CellAt(BPoint where, int& row, int& col) const;

private:
	TDialogEditPattern* fDialog;
	bool fSample;
	bool fTracking = false;
	bool fWhite = false;
};


class TDialogEditPattern : public TInkEditor {
public:
	TDialogEditPattern(const Ink* ink, BLooper* target, const BMessage& reply)
		:
		TInkEditor(3, target, reply)
	{
		if (ink != NULL && !ink->gradient)
			fOriginal = PatternFromInk(*ink);
		else {
			for (Color& c : fOriginal.pixels)
				c = Color{255, 255, 255, 255};
		}
		fGrid = fOriginal;
	}

	const PatternGrid& Grid() const { return fGrid; }

	// TDialogEditPattern::HandleCellAction
	void CellAction(int row, int col, bool white)
	{
		int index = row * fGrid.cols + col;
		if (fTool == 6) {
			fPicker->SetValue(ToRGB(fGrid.At(index)));
			return;
		}
		Color now = white ? Color{255, 255, 255, 255} : FromRGB(fPicker->ValueAsColor());
		if (fTool == 7)
			ReplacePatternColor(fGrid, fGrid.At(index), now);
		else
			fGrid.pixels[size_t(index)] = now;
		SetDirty(true);
		fEdit->Invalidate();
		fSample->Invalidate();
	}

protected:
	// TDialogEditPattern::Init: a new pattern's title (New Pattern), the
	// pencil chosen, the picker on the first pixel's colour
	virtual void Init()
	{
		if (fNew)
			SetTitle(TitleString(1).String());
		fEdit = new TPatternEditView(this, false);
		FillUser(Item(0), fEdit);
		fSample = new TPatternEditView(this, true);
		FillUser(Item(2), fSample);
		fPicker = MakePicker(3, 1, ToRGB(fGrid.At(0)));
		PutInPlace(Item(1), fPicker);
		static const int32 kIcons[3] = {80, 82, 81};
		for (int32 i = 0; i < 3; i++) {
			fTools[i] = new TToolButton(kIcons[i], ItemMessage(3, 5 + i));
			PutInPlace(Item(5 + i), fTools[i]);
		}
		ChooseTool(5);
		CheckPopups();
		SetDirty(false);
	}

	virtual bool ItemHit(int32 index, BMessage* message)
	{
		switch (index) {
			case 3:
			case 4:
			{
				int32 value = message->GetInt32(kValueString, 8);
				if (value != 8 && value != 16 && value != 32 && value != 64)
					value = 8;
				ResizePattern(fGrid, index == 3 ? value : fGrid.cols,
					index == 4 ? value : fGrid.rows);
				CheckPopups();
				SetDirty(true);
				fEdit->Invalidate();
				fSample->Invalidate();
				return true;
			}
			case 5:
			case 6:
			case 7:
				ChooseTool(index);
				return true;
			case 8:
			{
				// TPattern::Optimize, then TPenInk(TPattern) archived: the
				// port keeps 8 x 8 two-colour patterns only (STR# 6, 10)
				Ink ink;
				if (!OptimizePattern(fGrid, ink)) {
					(new BAlert("", GobeResources::IndexString(6, 10).String(), "OK"))->Go(NULL);
					return true;
				}
				Post(ink);
				Close();
				return false;
			}
			case 9:
				Close();
				return false;
			case 10:
				fGrid = fOriginal;
				CheckPopups();
				SetDirty(false);
				fEdit->Invalidate();
				fSample->Invalidate();
				return true;
		}
		return true;
	}

	virtual void Script(const std::string& command)
	{
		std::vector<float> n = Numbers(command);
		BMessage hit('APPL');
		if (command.compare(0, 6, "color ") == 0 && n.size() == 3)
			fPicker->SetValue(make_color(uint8(n[0]), uint8(n[1]), uint8(n[2]), 255));
		else if (command.compare(0, 5, "tool ") == 0 && n.size() == 1)
			ItemHit(int32(n[0]), &hit);
		else if (command.compare(0, 5, "cell ") == 0 && n.size() == 2)
			CellAction(int(n[0]), int(n[1]), false);
		else if (command.compare(0, 5, "size ") == 0 && n.size() == 2) {
			hit.AddInt32(kValueString, int32(n[0]));
			ItemHit(3, &hit);
			hit.ReplaceInt32(kValueString, int32(n[1]));
			ItemHit(4, &hit);
		} else if (command == "ok" || command == "cancel" || command == "revert")
			ItemHit(command == "ok" ? 8 : command == "cancel" ? 9 : 10, &hit);
		else if (command == "show") {
			printf("inkeditor pattern \"%s\" %d x %d tool %d dirty %d first %d %d %d\n",
				Title(), fGrid.cols, fGrid.rows, int(fTool), int(fDirty), fGrid.At(0).r,
				fGrid.At(0).g, fGrid.At(0).b);
			fflush(stdout);
		}
	}

private:
	void ChooseTool(int32 tool)
	{
		fTool = tool;
		for (int32 i = 0; i < 3; i++)
			if (fTools[i] != NULL)
				fTools[i]->SetValue(5 + i == tool ? B_CONTROL_ON : B_CONTROL_OFF);
	}

	// TDialogEditPattern::CheckPopup: Width and Height marked
	void CheckPopups()
	{
		SetPopUpItem(3, fGrid.cols);
		SetPopUpItem(4, fGrid.rows);
	}

	void SetDirty(bool dirty)
	{
		fDirty = dirty;
		SetItemEnabled(10, dirty);
	}

	PatternGrid fOriginal;
	PatternGrid fGrid;
	int32 fTool = 5;
	bool fDirty = false;
	BColorControl* fPicker = NULL;
	TPatternEditView* fEdit = NULL;
	TPatternEditView* fSample = NULL;
	TToolButton* fTools[3] = {};
};


bool
TPatternEditView::CellAt(BPoint where, int& row, int& col) const
{
	const PatternGrid& grid = fDialog->Grid();
	BRect r = Bounds();
	float cell = std::floor(std::min(r.Width() / grid.cols, r.Height() / grid.rows));
	if (cell < 1)
		return false;
	col = int(where.x / cell);
	row = int(where.y / cell);
	return col >= 0 && row >= 0 && col < grid.cols && row < grid.rows;
}


// UserEditDrawProc: the pixels as cells with a grid between them;
// UserSampleDrawProc: the pattern repeated
void
TPatternEditView::Draw(BRect)
{
	const PatternGrid& grid = fDialog->Grid();
	BRect r = Bounds();
	if (fSample) {
		for (float y = r.top; y <= r.bottom; y += grid.rows)
			for (float x = r.left; x <= r.right; x += grid.cols)
				for (int i = 0; i < grid.rows * grid.cols; i++) {
					SetHighColor(ToRGB(grid.At(i)));
					FillRect(BRect(x + i % grid.cols, y + i / grid.cols, x + i % grid.cols,
						y + i / grid.cols));
				}
		SetHighColor(0, 0, 0);
		StrokeRect(r);
		return;
	}
	SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(r);
	float cell = std::floor(std::min(r.Width() / grid.cols, r.Height() / grid.rows));
	for (int i = 0; i < grid.rows * grid.cols; i++) {
		BRect c(i % grid.cols * cell, i / grid.cols * cell, (i % grid.cols + 1) * cell,
			(i / grid.cols + 1) * cell);
		SetHighColor(ToRGB(grid.At(i)));
		FillRect(c);
		if (cell >= 4) {
			SetHighColor(128, 128, 128);
			StrokeRect(c);
		}
	}
}


// UserEditClickProc: the tool on the cell, the pencil also on those
// dragged over
void
TPatternEditView::MouseDown(BPoint where)
{
	if (fSample)
		return;
	int row, col;
	if (!CellAt(where, row, col))
		return;
	uint32 buttons = 0;
	if (Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
	fWhite = (buttons & B_SECONDARY_MOUSE_BUTTON) != 0;
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	fDialog->CellAction(row, col, fWhite);
}


void
TPatternEditView::MouseMoved(BPoint where, uint32, const BMessage*)
{
	int row, col;
	if (fTracking && CellAt(where, row, col))
		fDialog->CellAction(row, col, fWhite);
}


// ---- TDialogEditGradient (DIAL 4): OK 0, Cancel 1, Revert 2, the edit
// view 3 (a click sets a directional gradient's angle or a radial one's
// center), the sample 4, Type 5 (MENU 245), Angle 6 (0-360, STR# 6, 13),
// Colors 7 (MENU 246: 2-4), the colours 8-11 (T256ColorPopUp: those past
// the count disabled)

class TDialogEditGradient;

class TGradientView : public BView {
public:
	TGradientView(TDialogEditGradient* dialog, bool edit)
		:
		BView(edit ? "edit" : "sample", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fDialog(dialog),
		fEdit(edit)
	{
	}

	virtual void Draw(BRect);
	virtual void MouseDown(BPoint where);
	virtual void MouseMoved(BPoint where, uint32, const BMessage*)
	{
		if (fTracking)
			Click(where);
	}
	virtual void MouseUp(BPoint)
	{
		fTracking = false;
	}

	void Click(BPoint where);

private:
	TDialogEditGradient* fDialog;
	bool fEdit;
	bool fTracking = false;
};


// T256ColorPopUp: a colour well whose menu is the colour set
class TColorWell : public BControl {
public:
	TColorWell(BMessage* message)
		:
		BControl("color", NULL, message, B_WILL_DRAW)
	{
		SetExplicitMinSize(BSize(22, 18));
		SetExplicitMaxSize(BSize(22, 18));
	}

	rgb_color ColorValue() const { return fColor; }
	void SetColorValue(rgb_color color)
	{
		fColor = color;
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		BRect r = Bounds();
		SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillRect(r);
		SetHighColor(IsEnabled() ? fColor : tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_1_TINT));
		FillRect(r.InsetByCopy(2, 2));
		SetHighColor(IsEnabled() ? make_color(0, 0, 0) : make_color(160, 160, 160));
		StrokeRect(r.InsetByCopy(1, 1));
	}

	virtual void MouseDown(BPoint)
	{
		if (!IsEnabled())
			return;
		BMessage model('Gcol');
		std::unique_ptr<BMenu> set(ColorSetMenu(model));
		BPopUpMenu* menu = new BPopUpMenu("colors", false, false);
		while (BMenuItem* item = set->RemoveItem(int32(0)))
			menu->AddItem(item);
		BMenuItem* chosen = menu->Go(ConvertToScreen(Bounds().LeftBottom()), false, true);
		const void* data;
		ssize_t size;
		if (chosen != NULL && chosen->Message() != NULL
				&& chosen->Message()->FindData("color", B_RGB_COLOR_TYPE, &data, &size) == B_OK) {
			SetColorValue(*(const rgb_color*)data);
			Invoke();
		}
		delete menu;
	}

private:
	rgb_color fColor = {0, 0, 0, 255};
};


class TDialogEditGradient : public TInkEditor {
public:
	TDialogEditGradient(const Ink* ink, BLooper* target, const BMessage& reply)
		:
		TInkEditor(4, target, reply),
		fOriginal(ink != NULL && ink->gradient ? *ink : NewGradient()),
		fInk(fOriginal)
	{
	}

	const Ink& Gradient() const { return fInk; }

	void EditClick(float x, float y, float width, float height)
	{
		GradientClick(fInk, x, y, width, height);
		GradientChange();
	}

protected:
	// TDialogEditGradient::InitPostRecalc: a new gradient's title (New
	// Gradient)
	virtual void Init()
	{
		if (fNew)
			SetTitle(TitleString(2).String());
		fEdit = new TGradientView(this, true);
		FillUser(Item(3), fEdit);
		fSample = new TGradientView(this, false);
		FillUser(Item(4), fSample);
		for (int32 i = 0; i < 4; i++) {
			fWells[i] = new TColorWell(ItemMessage(4, 8 + i));
			PutInPlace(Item(8 + i), fWells[i]);
		}
		GradientChange();
	}

	virtual bool ItemHit(int32 index, BMessage* message)
	{
		switch (index) {
			case 0:
				if (!ValidAngleText())
					return true;
				Post(fInk);
				Close();
				return false;
			case 1:
				Close();
				return false;
			case 2:
				fInk = fOriginal;
				break;
			case 5:
				SetGradientType(fInk, uint32(message->GetInt32(kValueString, 0)));
				break;
			case 6:
				if (!ValidAngleText())
					return true;
				break;
			case 7:
				fInk.colorCount = std::max(2, std::min(4, int(message->GetInt32(kValueString,
					2))));
				break;
			case 8:
			case 9:
			case 10:
			case 11:
				fInk.colors[index - 8] = FromRGB(fWells[index - 8]->ColorValue());
				fInk.color = fInk.colors[0];
				break;
			default:
				return true;
		}
		GradientChange();
		return true;
	}

	virtual void Script(const std::string& command)
	{
		std::vector<float> n = Numbers(command);
		BMessage hit('APPL');
		if (command.compare(0, 5, "type ") == 0 && command.size() == 9) {
			const char* t = command.c_str() + 5;
			hit.AddInt32(kValueString, int32(uint32(t[0]) << 24 | uint32(t[1]) << 16
				| uint32(t[2]) << 8 | uint32(t[3])));
			ItemHit(5, &hit);
		} else if (command.compare(0, 6, "angle ") == 0) {
			SetTextItem(6, command.c_str() + 6);
			ItemHit(6, &hit);
		} else if (command.compare(0, 7, "colors ") == 0 && n.size() == 1) {
			hit.AddInt32(kValueString, int32(n[0]));
			ItemHit(7, &hit);
		} else if (command.compare(0, 5, "stop ") == 0 && n.size() == 4 && n[0] >= 0
				&& n[0] < 4) {
			int i = int(n[0]);
			fWells[i]->SetColorValue(make_color(uint8(n[1]), uint8(n[2]), uint8(n[3]), 255));
			ItemHit(8 + i, &hit);
		} else if (command.compare(0, 6, "click ") == 0 && n.size() == 2) {
			BRect b = fEdit->Bounds();
			EditClick(n[0], n[1], b.Width(), b.Height());
		} else if (command == "ok" || command == "cancel" || command == "revert")
			ItemHit(command == "ok" ? 0 : command == "cancel" ? 1 : 2, &hit);
		else if (command == "show") {
			printf("inkeditor gradient \"%s\" %s colors %d angle %.1f center %.2f %.2f first "
				"%d %d %d\n", Title(), fInk.gradientType == Sig("GDrl") ? "GDrl" : "GDdr",
				fInk.colorCount, fInk.angle, fInk.center[0], fInk.center[1], fInk.colors[0].r,
				fInk.colors[0].g, fInk.colors[0].b);
			fflush(stdout);
		}
	}

private:
	// TDialogEditGradient::GradientChange: the type and count marked, the
	// angle shown, the colours (those past the count disabled), drawn again
	void GradientChange()
	{
		SetPopUpItem(5, int32(fInk.gradientType));
		SetAngleText();
		SetPopUpItem(7, fInk.colorCount);
		for (int32 i = 0; i < 4; i++) {
			if (fWells[i] == NULL)
				continue;
			fWells[i]->SetColorValue(ToRGB(fInk.colors[i]));
			fWells[i]->SetEnabled(i < fInk.colorCount);
		}
		if (fEdit != NULL)
			fEdit->Invalidate();
		if (fSample != NULL)
			fSample->Invalidate();
	}

	// SetAngleText: one decimal
	void SetAngleText()
	{
		BString text;
		text.SetToFormat("%.1f", fInk.angle);
		SetTextItem(6, text.String());
	}

	// ValidAngleText: 0 to 360, else STR# 6, 13
	bool ValidAngleText()
	{
		BString text = TextItem(6);
		char* end;
		float angle = strtof(text.String(), &end);
		if (text.Length() == 0 || *end != '\0' || angle < 0 || angle > 360) {
			char alert[256];
			snprintf(alert, sizeof(alert), GobeResources::IndexString(6, 13).String(), 0, 360);
			(new BAlert("", alert, "OK"))->Go(NULL);
			MakeFocusItem(6);
			return false;
		}
		fInk.angle = angle;
		return true;
	}

	Ink fOriginal;
	Ink fInk;
	TGradientView* fEdit = NULL;
	TGradientView* fSample = NULL;
	TColorWell* fWells[4] = {};
};


// EditDrawProc (TGradient::EditDraw: the gradient and its handle, a line
// from the middle at the angle or a mark at the center); SampleDrawProc
void
TGradientView::Draw(BRect)
{
	BRect r = Bounds();
	const Ink& ink = fDialog->Gradient();
	FillInk(this, r, ink);
	SetHighColor(0, 0, 0);
	StrokeRect(r);
	if (!fEdit)
		return;
	SetDrawingMode(B_OP_INVERT);
	if (ink.gradientType == Sig("GDrl")) {
		BPoint c(r.left + r.Width() * ink.center[0], r.top + r.Height() * ink.center[1]);
		StrokeEllipse(c, 4, 4);
	} else {
		BPoint c(r.left + r.Width() / 2, r.top + r.Height() / 2);
		float a = ink.angle * M_PI / 180;
		BPoint end = c + BPoint(cosf(a) * r.Width() / 2.5f, -sinf(a) * r.Height() / 2.5f);
		StrokeLine(c, end);
		StrokeEllipse(end, 3, 3);
	}
	SetDrawingMode(B_OP_COPY);
}


void
TGradientView::MouseDown(BPoint where)
{
	if (!fEdit)
		return;
	fTracking = true;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	Click(where);
}


void
TGradientView::Click(BPoint where)
{
	BRect b = Bounds();
	fDialog->EditClick(where.x - b.left, where.y - b.top, b.Width(), b.Height());
}


// the clipboard (TDialogNewColor +0x80, +0x84, +0x88)
struct Clip {
	int kind = -1;
	bool hasInk = false;
	Ink ink;
	bool hasPen = false;
	PenType pen;
};

Clip sClip;

} // namespace


void
EditInk(int minorType, const Ink* ink, BLooper* target, const BMessage& reply)
{
	TInkEditor* editor;
	if (minorType == 1)
		editor = new TDialogEditPattern(ink, target, reply);
	else if (minorType == 2)
		editor = new TDialogEditGradient(ink, target, reply);
	else if (minorType == 0) {
		// a new colour starts white (DoEditInkDialog)
		rgb_color color = {255, 255, 255, 255};
		if (ink != NULL && !ink->gradient)
			color = ToRGB(ink->color);
		editor = new TDialogEditColor(color, target, reply);
	} else
		return;
	editor->RunDialog();
}


void
InkEditorScript(const std::string& command)
{
	if (sEditor == NULL) {
		printf("inkeditor: no ink editor\n");
		fflush(stdout);
		return;
	}
	BMessage script(kMsgScript);
	script.AddString("command", command.c_str());
	sEditor->PostMessage(&script);
}


void
InkClipboard::CopyInk(int minorType, const Ink& ink)
{
	sClip.kind = minorType;
	sClip.ink = ink;
	sClip.hasInk = true;
}


void
InkClipboard::CopyPen(const PenType& pen)
{
	sClip.kind = 3;
	sClip.pen = pen;
	sClip.hasPen = true;
}


bool
InkClipboard::TakeColor(const BMessage* message)
{
	const void* data;
	ssize_t size;
	if (message == NULL
			|| message->FindData("RGBColor", B_RGB_COLOR_TYPE, &data, &size) != B_OK
			|| size < 4)
		return false;
	Ink ink;
	ink.set = true;
	ink.color = FromRGB(*(const rgb_color*)data);
	ink.low = Color{255, 255, 255, 255};
	CopyInk(0, ink);
	return true;
}


int
InkClipboard::Kind()
{
	return sClip.kind;
}


bool
InkClipboard::HasInk()
{
	return sClip.hasInk;
}


const Ink&
InkClipboard::CopiedInk()
{
	return sClip.ink;
}


bool
InkClipboard::HasPen()
{
	return sClip.hasPen;
}


const PenType&
InkClipboard::CopiedPen()
{
	return sClip.pen;
}


// ---- TPenWidthView

namespace {

class TRepeatButton : public BControl {
public:
	TRepeatButton(int32 micn, BMessage* message)
		:
		BControl("width", NULL, message, B_WILL_DRAW),
		fIcon(TMICNElement::LoadIcon(micn))
	{
		SetExplicitMinSize(BSize(19, 19));
		SetExplicitMaxSize(BSize(19, 19));
	}

	virtual void Draw(BRect)
	{
		BRect r = Bounds();
		if (Value() == B_CONTROL_ON)
			TRibbon::DrawOutlineDown(this, r);
		else {
			SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
			FillRect(r);
		}
		if (fIcon != NULL) {
			SetDrawingMode(B_OP_OVER);
			DrawBitmap(fIcon.get(), BPoint(r.left + 1, r.top + 1));
			SetDrawingMode(B_OP_COPY);
		}
	}

	virtual void MouseDown(BPoint)
	{
		if (!IsEnabled())
			return;
		SetValue(B_CONTROL_ON);
		Invoke();
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	}

	virtual void MouseUp(BPoint)
	{
		SetValue(B_CONTROL_OFF);
	}

private:
	std::unique_ptr<BBitmap> fIcon;
};


// the slider's value for a width and back (TPenView::SetPenWidth; the
// 'PEN#' handler): past 31 a step is 7 points
int32
SliderValue(float width)
{
	if (width > 31)
		width = (width - 32) / 7 + 32;
	return int32(std::lround(width));
}


float
SliderWidth(int32 value)
{
	return value > 31 ? float((value - 32) * 7 + 32) : float(value);
}


TPageView*
FrontPages(TDocWindow* window)
{
	return window != NULL ? dynamic_cast<TPageView*>(window->FindView("TPageView")) : NULL;
}

} // namespace


TPenWidthView::TPenWidthView()
	:
	BView("PenView", B_WILL_DRAW)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fLabel = new BStringView("width", "");
	fLabel->SetAlignment(B_ALIGN_CENTER);
	fSmaller = new TRepeatButton(0x22, new BMessage('PEN-'));
	fLarger = new TRepeatButton(0x27, new BMessage('PEN+'));
	fSlider = new BSlider("slider", NULL, new BMessage('PEN#'), 0, 64, B_VERTICAL);
	fSlider->SetModificationMessage(new BMessage('PEN%'));
	fSlider->SetBarColor(make_color(0xee, 0xee, 0xee));
	fSlider->SetExplicitMaxSize(BSize(30, B_SIZE_UNLIMITED));
	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 2);
	SetLayout(layout);
	layout->SetInsets(2, 2, 2, 2);
	layout->AddView(fLabel);
	BGroupLayout* row = new BGroupLayout(B_HORIZONTAL, 2);
	layout->AddItem(row);
	BGroupLayout* buttons = new BGroupLayout(B_VERTICAL, 2);
	row->AddItem(buttons);
	buttons->AddView(fSmaller);
	buttons->AddView(fLarger);
	buttons->AddItem(BSpaceLayoutItem::CreateGlue());
	row->AddView(fSlider);
	SetPenWidth(0);
}


void
TPenWidthView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fSmaller->SetTarget(this);
	fLarger->SetTarget(this);
	fSlider->SetTarget(this);
	Update();
}


void
TPenWidthView::SetPenWidth(float width)
{
	fWidth = std::max(0.0f, std::min(255.0f, width));
	BString text;
	if (fWidth == 0)
		text = GobeResources::IndexString(11, 34);
	else
		text.SetToFormat("%.1f %s", fWidth, GobeResources::IndexString(7, 4).String());
	fLabel->SetText(text.String());
	fSlider->SetValue(SliderValue(fWidth));
}


void
TPenWidthView::Update()
{
	TDocWindow* window = TDocWindow::Front();
	if (window == NULL || window->LockWithTimeout(100000) != B_OK)
		return;
	TPageView::SelectedPen state;
	if (TPageView* pages = FrontPages(window))
		pages->SelectedPens(state);
	window->Unlock();
	if (state.any && state.size != fWidth)
		SetPenWidth(state.size);
}


void
TPenWidthView::Post(float width)
{
	SetPenWidth(width);
	TDocWindow* window = TDocWindow::Front();
	if (window == NULL)
		return;
	BMessage size('sf09');
	size.AddFloat(kValueString, fWidth);
	window->PostMessage(&size);
}


TInkOpacityView::TInkOpacityView()
	:
	BView("InkView", B_WILL_DRAW)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fLabel = new BStringView("opacity", "100%");
	fLabel->SetAlignment(B_ALIGN_CENTER);
	fMore = new TRepeatButton(0x21, new BMessage('TRN-'));
	fLess = new TRepeatButton(0x20, new BMessage('TRN+'));
	fSlider = new BSlider("slider", NULL, new BMessage('TRN#'), 0, 100, B_VERTICAL);
	fSlider->SetModificationMessage(new BMessage('TRN%'));
	fSlider->SetBarColor(make_color(0xee, 0xee, 0xee));
	// (TVerticalSlider: made 21 by 50)
	fSlider->SetExplicitMinSize(BSize(B_SIZE_UNSET, 50));
	fSlider->SetExplicitMaxSize(BSize(30, B_SIZE_UNLIMITED));
	// (TInkView::PositionSubViews: the label, a button under it, the
	// slider, the other button at the bottom, in a column)
	BGroupLayout* layout = new BGroupLayout(B_VERTICAL, 3);
	SetLayout(layout);
	layout->SetInsets(4, 2, 4, 2);
	layout->AddView(fLabel);
	layout->AddView(fMore, 0.0f);
	layout->AddView(fSlider);
	layout->AddView(fLess, 0.0f);
	layout->SetItemWeight(2, 1);
	fLabel->SetExplicitMinSize(BSize(fLabel->StringWidth("100%") + 4, B_SIZE_UNSET));
	SetOpacity(100);
}


void
TInkOpacityView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fMore->SetTarget(Window());
	fLess->SetTarget(Window());
	fSlider->SetTarget(Window());
}


void
TInkOpacityView::SetOpacity(int32 opacity)
{
	fOpacity = std::max(int32(0), std::min(int32(100), opacity));
	BString text;
	text.SetToFormat("%d%%", int(fOpacity));
	fLabel->SetText(text.String());
	// (TVerticalSlider's transparency runs down from 0 at the top; a
	// vertical BSlider's values run up, so it takes the opacity)
	fSlider->SetValue(fOpacity);
}


int32
TInkOpacityView::Transparency() const
{
	return 100 - fSlider->Value();
}


void
TPenWidthView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'PEN-':
		case 'PEN+':
			Post(fWidth + (message->what == 'PEN+' ? 1 : -1));
			return;
		case 'PEN#':
		case 'PEN%':
			Post(SliderWidth(fSlider->Value()));
			return;
	}
	BView::MessageReceived(message);
}

} // namespace gobe
