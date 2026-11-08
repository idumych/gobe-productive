#include "TImageGIMP.h"

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageQueue.h>
#include <Node.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <Roster.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <fs_attr.h>
#include <image.h>

#include <fstream>
#include <set>
#include <string>
#include <unistd.h>

#include "GobeResources.h"
#include "TImagePlugins.h"
#include "TStatusDialog.h"

extern char** environ;

namespace gobe {

namespace {

// the plug-in library's messages (its msg.h), laid out as this system's
// compiler lays them out for the filters
enum {
	kQuit, kImageNew, kImageDisplay, kImageInput, kImageOutput, kImageColor, kImageUpdate,
	kLoad, kSave, kParams, kProgress, kMessage, kDialog
};
enum { kParamsGet, kParamsSet };
enum { kTypeRGB = 1, kTypeGray = 2, kTypeIndexed = 4 };
enum {
	kDialogNew, kDialogShow, kDialogUpdate, kDialogClose, kDialogNewItem, kDialogShowItem,
	kDialogHideItem, kDialogChangeItem, kDialogDeleteItem, kDialogCallback
};
enum {
	kGroupRows = 1, kGroupColumns, kGroupForm, kItemPushButton, kItemCheckButton,
	kItemRadioButton, kItemImageMenu, kItemScale, kItemFrame, kItemLabel, kItemText,
	kGroupRadio = 1 << 16
};

struct MsgHeader {
	int32 type;
	long size;
};
struct MsgParams {
	int32 type;
	long size;
	void* data;
};
struct MsgProgress {
	long current;
	long max;
	char label[256];
};
struct MsgImage {
	int32 type;
	char name[256];
	int ID, shmid;
	int width, height, channels;
	int x1, y1, x2, y2;
	long data, colors;
	unsigned char cmap[768];
};
struct MsgColor {
	int32 type;
	unsigned char r, g, b;
};
struct DialogMsg {
	int32 type;
	int dialog_ID;
	char title[256];
};
struct DialogItemMsg {
	int32 type;
	int dialog_ID;
	int parent_ID;
	int item_ID;
	int item_type;
	char data[256];
};
struct DialogCallbackMsg {
	int32 type;
	int dialog_ID;
	int item_ID;
	char data[256];
};
union MsgDialog {
	int32 type;
	DialogMsg dialog;
	DialogItemMsg item;
	DialogCallbackMsg callback;
};


const ResourceFile&
ImageResources()
{
	return GobeResources::Part("ImageProcessing");
}


// GIMPPlugin::GIMPDir's folders: next to the application as packaged or as
// built here, then the user's add-ons
std::vector<BString>
GIMPDirectories()
{
	std::vector<BString> dirs;
	app_info info;
	if (be_app != NULL && be_app->GetAppInfo(&info) == B_OK) {
		BPath app(&info.ref);
		if (app.GetParent(&app) == B_OK) {
			dirs.push_back(BString(app.Path()) << "/add-ons/GIMP");
			dirs.push_back(BString(app.Path()) << "/../add-ons/GIMP");
			dirs.push_back(BString(app.Path()) << "/plugins/build/GIMP");
		}
	}
	BPath user;
	if (find_directory(B_USER_ADDONS_DIRECTORY, &user) == B_OK) {
		user.Append("GIMP");
		dirs.push_back(user.Path());
	}
	return dirs;
}


BString
ReadStringAttr(BNode& node, const char* name)
{
	attr_info info;
	if (node.GetAttrInfo(name, &info) != B_OK || info.type != B_STRING_TYPE || info.size <= 0)
		return "";
	BString value;
	char* buffer = value.LockBuffer(info.size + 1);
	ssize_t read = node.ReadAttr(name, B_STRING_TYPE, 0, buffer, info.size);
	buffer[read > 0 ? read : 0] = '\0';
	value.UnlockBuffer();
	return value;
}


// the GIMP folder's index (the attributes the original's filters carry,
// for systems where they're lost): file, name, category, description,
// author, address
std::map<std::string, std::vector<std::string>>
ReadIndex(const char* dir)
{
	std::map<std::string, std::vector<std::string>> index;
	std::ifstream in((std::string(dir) + "/index").c_str());
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		std::vector<std::string> fields;
		size_t at = 0;
		while (true) {
			size_t tab = line.find('\t', at);
			std::string field = line.substr(at, tab == std::string::npos ? std::string::npos
				: tab - at);
			size_t escape;
			while ((escape = field.find("\\n")) != std::string::npos)
				field.replace(escape, 2, "\n");
			fields.push_back(field);
			if (tab == std::string::npos)
				break;
			at = tab + 1;
		}
		if (fields.size() >= 2)
			index[fields[0]] = fields;
	}
	return index;
}


// the filters' settings between runs (TPrefStore "GIMP-plugin"), by name
BPath
ParamsPath()
{
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	path.Append("Gobe Productive GIMP-plugin");
	return path;
}

std::mutex sParamsLock;

} // namespace


// #pragma mark - the plug-in table


void
AddGIMPPlugins(std::vector<ImagePlugin>& plugins)
{
	std::set<std::string> seen;
	for (const BString& dir : GIMPDirectories()) {
		BDirectory directory(dir.String());
		if (directory.InitCheck() != B_OK)
			continue;
		std::map<std::string, std::vector<std::string>> index = ReadIndex(dir.String());
		BEntry entry;
		while (directory.GetNextEntry(&entry, true) == B_OK) {
			char leaf[B_FILE_NAME_LENGTH];
			BPath path;
			if (!entry.IsFile() || entry.GetName(leaf) != B_OK || entry.GetPath(&path) != B_OK
					|| strcmp(leaf, "index") == 0 || access(path.Path(), X_OK) != 0
					|| seen.count(leaf))
				continue;
			seen.insert(leaf);
			BNode node(&entry);
			ImagePlugin plugin;
			plugin.kind = 0;
			plugin.path = path.Path();
			plugin.name = ReadStringAttr(node, "name");
			plugin.category = ReadStringAttr(node, "category");
			auto found = index.find(leaf);
			if (found != index.end()) {
				const std::vector<std::string>& f = found->second;
				if (plugin.name.Length() == 0 && f.size() > 1)
					plugin.name = f[1].c_str();
				if (plugin.category.Length() == 0 && f.size() > 2)
					plugin.category = f[2].c_str();
			}
			if (plugin.name.Length() == 0)
				plugin.name = leaf;
			// (no category: STR# 2's "Miscellaneous")
			if (plugin.category.Length() == 0)
				plugin.category = IndexString(ImageResources(), 2, 1).c_str();
			plugins.push_back(plugin);
		}
	}
}


// GIMPPlugin::Construct's attributes, or the index's
static void
GIMPInfo(const ImagePlugin& plugin, BString& description, BString& author, BString& email)
{
	BNode node(plugin.path.String());
	description = ReadStringAttr(node, "GIMP_DESC");
	author = ReadStringAttr(node, "GIMP_AUTHOR");
	email = ReadStringAttr(node, "GIMP_EMAIL");
	BPath path(plugin.path.String()), dir;
	path.GetParent(&dir);
	std::map<std::string, std::vector<std::string>> index = ReadIndex(dir.Path());
	auto found = index.find(path.Leaf());
	if (found == index.end())
		return;
	const std::vector<std::string>& f = found->second;
	if (description.Length() == 0 && f.size() > 3)
		description = f[3].c_str();
	if (author.Length() == 0 && f.size() > 4)
		author = f[4].c_str();
	if (email.Length() == 0 && f.size() > 5)
		email = f[5].c_str();
}


// GIMPPlugin::About: the name, "by" the author and <address> on lines of
// their own, the description
static void
ShowAbout(const BString& name, const BString& description, const BString& author,
	const BString& email)
{
	BString text = name;
	if (author.Length() > 0 || email.Length() > 0)
		text << "\nby ";
	if (author.Length() > 0)
		text << author << "\n";
	if (email.Length() > 0)
		text << " <" << email << ">\n\n";
	else
		text << "\n\n";
	text << description;
	StringAlert(text.String());
}


void
GIMPAbout(const ImagePlugin& plugin)
{
	BString description, author, email;
	GIMPInfo(plugin, description, author, email);
	ShowAbout(plugin.name, description, author, email);
}


// #pragma mark - TGIMPDialog


// ScaleEngineer: its label (once the filter gives it one) over the value
// in a text field five digits wide and a slider beside it, block thumb,
// three hash marks below, its limits under the ends
class TGIMPScale : public BGroupView {
public:
	TGIMPScale(int32 min, int32 max, int32 value, uint32 what)
		:
		BGroupView(B_VERTICAL, 2),
		fMin(min),
		fMax(max)
	{
		GroupLayout()->SetInsets(0);
		fText = new BTextControl("value", NULL, "", new BMessage(what));
		fSlider = new BSlider("slider", NULL, new BMessage(what), min, max, B_HORIZONTAL,
			B_BLOCK_THUMB);
		fSlider->SetModificationMessage(new BMessage(what));
		fSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		fSlider->SetHashMarkCount(3);
		BString low, high;
		low << min;
		high << max;
		fSlider->SetLimitLabels(low.String(), high.String());
		fSlider->SetExplicitMinSize(BSize(100, B_SIZE_UNSET));
		float digits = 5 * fText->TextView()->StringWidth("0") + 8;
		fText->TextView()->SetExplicitMinSize(BSize(digits, B_SIZE_UNSET));
		fText->TextView()->SetExplicitMaxSize(BSize(digits, B_SIZE_UNSET));
		BGroupView* row = new BGroupView(B_HORIZONTAL, 8);
		row->GroupLayout()->AddView(fText, 0);
		row->GroupLayout()->AddView(fSlider, 1);
		row->GroupLayout()->SetInsets(0);
		fText->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
		GroupLayout()->AddView(row);
		SetValue(value < min || value > max ? min : value);
	}

	void SetLabel(const char* label)
	{
		if (fLabel == NULL) {
			fLabel = new BStringView("label", label);
			GroupLayout()->AddView(0, fLabel);
		} else
			fLabel->SetText(label);
	}

	void SetValue(int32 value)
	{
		fSlider->SetValue(value);
		BString text;
		text << value;
		fText->SetText(text.String());
	}

	// (the text's value when it was typed into, else the slider's)
	int32 Value(bool fromText)
	{
		if (fromText) {
			int32 value = int32(strtol(fText->Text(), NULL, 0));
			fSlider->SetValue(value);
			return fSlider->Value();
		}
		BString text;
		text << fSlider->Value();
		if (text != fText->Text())
			fText->SetText(text.String());
		return fSlider->Value();
	}

	// ScaleEngineer::Validate: a number
	bool Valid() const
	{
		char* end = NULL;
		strtol(fText->Text(), &end, 0);
		return end == NULL || *end == '\0';
	}

	BTextControl* Text() const { return fText; }
	BSlider* Slider() const { return fSlider; }

private:
	int32 fMin, fMax;
	BStringView* fLabel = NULL;
	BTextControl* fText;
	BSlider* fSlider;
};


// GIMPDialog: "Plugin: " and the filter's title, beside the document
// window, the filter's items over About…, Cancel and OK; the items are
// numbered as the filter knows them (0 the items' group, 1 OK, 2 Cancel,
// 3 About…), their messages 'sD00' and the number, each a callback to the
// filter, which closes it
class TGIMPDialog : public BWindow {
public:
	TGIMPDialog(GIMPRun* run, int32 id, const char* title, BWindow* window, BRect frame,
		const char* documentName, bool hasAbout)
		:
		BWindow(BRect(0, 0, 10, 10), "", B_TITLED_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
			B_NOT_RESIZABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fRun(run),
		fID(id),
		fDocumentName(documentName)
	{
		BString name = IndexString(ImageResources(), 2, 3).c_str();
		name << title;
		SetTitle(name.String());
		SetLayout(new BGroupLayout(B_VERTICAL, 0));
		BGroupView* background = new BGroupView(B_VERTICAL, 10);
		AddChild(background);
		BGroupLayout* root = background->GroupLayout();
		root->SetInsets(10);
		BGroupView* content = new BGroupView(B_VERTICAL, 5);
		content->GroupLayout()->SetInsets(0);
		root->AddView(content);
		BGroupView* buttons = new BGroupView(B_HORIZONTAL, 10);
		buttons->GroupLayout()->SetInsets(0);
		root->AddView(buttons);
		BButton* about = new BButton("about", IndexString(ImageResources(), 2, 13).c_str(),
			new BMessage('sD00' + 3));
		BButton* cancel = new BButton("cancel", GobeResources::IndexString(2, 1).String(),
			new BMessage('sD00' + 2));
		BButton* ok = new BButton("ok", GobeResources::IndexString(2, 0).String(),
			new BMessage('sD00' + 1));
		buttons->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
		buttons->GroupLayout()->AddView(about);
		buttons->GroupLayout()->AddView(cancel);
		buttons->GroupLayout()->AddView(ok);
		about->SetExplicitMinSize(BSize(75, B_SIZE_UNSET));
		cancel->SetExplicitMinSize(BSize(75, B_SIZE_UNSET));
		ok->SetExplicitMinSize(BSize(75, B_SIZE_UNSET));
		SetDefaultButton(ok);
		if (!hasAbout)
			about->Hide();
		fItems[0] = Entry{kGroupRows, content, content->GroupLayout(), 0};
		fItems[1] = Entry{kItemPushButton, ok, NULL, 0};
		fItems[2] = Entry{kItemPushButton, cancel, NULL, 0};
		fItems[3] = Entry{kItemPushButton, about, NULL, 0};
		if (window != NULL)
			AddToSubset(window);
		MoveTo(frame.LeftTop() + BPoint(20, 20));
	}

	// handle_dialog DIALOG_NEW_ITEM: the item in its parent, numbered next
	int32 AddItem(int32 parent, int32 type, const char* data)
	{
		BAutolock lock(this);
		auto found = fItems.find(parent);
		if (found == fItems.end())
			return -1;
		Entry& into = found->second;
		int32 kind = type & 0x7fff;
		uint32 what = 'sD00' + uint32(fNextItem);
		Entry item{kind, NULL, NULL, parent};
		switch (kind) {
			case kGroupRows:
			case kGroupColumns: {
				BGroupView* group = new BGroupView(
					kind == kGroupRows ? B_VERTICAL : B_HORIZONTAL, 2);
				group->GroupLayout()->SetInsets(0);
				item.view = group;
				item.layout = group->GroupLayout();
				break;
			}
			case kItemPushButton:
				item.view = new BButton("button", data, new BMessage(what));
				break;
			case kItemCheckButton:
				item.view = new BCheckBox("check", data, new BMessage(what));
				break;
			case kItemRadioButton: {
				// (the first of its group on)
				BRadioButton* radio = new BRadioButton("radio", data, new BMessage(what));
				if (into.layout != NULL && into.layout->CountItems() == 0)
					radio->SetValue(B_CONTROL_ON);
				item.view = radio;
				break;
			}
			case kItemImageMenu: {
				// ImageListPopUp, "Images:": this one marked
				BPopUpMenu* menu = new BPopUpMenu("");
				BMenuItem* only = new BMenuItem(fDocumentName.String(), new BMessage(what));
				only->SetMarked(true);
				menu->AddItem(only);
				item.view = new BMenuField("images", IndexString(ImageResources(), 2, 4).c_str(),
					menu);
				break;
			}
			case kItemScale: {
				// (the filter's longs: minimum, maximum, value, decimals)
				const long* values = (const long*)data;
				item.view = new TGIMPScale(int32(values[0]), int32(values[1]), int32(values[2]),
					what);
				item.precision = int32(values[3]);
				break;
			}
			case kItemFrame: {
				BBox* box = new BBox("frame");
				if (data[0] != '\0')
					box->SetLabel(data);
				BGroupView* group = new BGroupView(B_VERTICAL, 2);
				group->GroupLayout()->SetInsets(6, 2, 6, 4);
				box->AddChild(group);
				item.view = box;
				item.layout = group->GroupLayout();
				break;
			}
			case kItemLabel:
				// (a text field's or a scale's: its label)
				if (BTextControl* text = dynamic_cast<BTextControl*>(into.view)) {
					text->SetLabel(data);
					return fNextItem++;
				}
				if (TGIMPScale* scale = dynamic_cast<TGIMPScale*>(into.view)) {
					scale->SetLabel(data);
					return fNextItem++;
				}
				item.view = new BStringView("label", data);
				break;
			case kItemText: {
				BTextControl* text = new BTextControl("text", NULL, data, new BMessage(what));
				item.view = text;
				break;
			}
			default:
				return fNextItem++;
		}
		if (into.layout == NULL) {
			delete item.view;
			return -1;
		}
		into.layout->AddView(item.view);
		int32 id = fNextItem++;
		fItems[id] = item;
		return id;
	}

	// DIALOG_CHANGE_ITEM: a scale's, a check's or radio's value, a text
	void ChangeItem(int32 id, const char* data)
	{
		BAutolock lock(this);
		auto found = fItems.find(id);
		if (found == fItems.end())
			return;
		int32 value;
		memcpy(&value, data, sizeof(value));
		BView* view = found->second.view;
		switch (found->second.type) {
			case kItemScale:
				if (static_cast<TGIMPScale*>(view)->Slider()->Value() != value)
					static_cast<TGIMPScale*>(view)->SetValue(value);
				break;
			case kItemCheckButton:
			case kItemRadioButton:
				static_cast<BControl*>(view)->SetValue(value);
				break;
			case kItemText:
				static_cast<BTextControl*>(view)->SetText(data);
				break;
		}
	}

	void ShowItem(int32 id, bool show)
	{
		BAutolock lock(this);
		auto found = fItems.find(id);
		if (found == fItems.end() || found->second.view == NULL)
			return;
		BView* view = found->second.view;
		if (show && view->IsHidden())
			view->Show();
		else if (!show && !view->IsHidden())
			view->Hide();
	}

	void DeleteItem(int32 id)
	{
		BAutolock lock(this);
		auto found = fItems.find(id);
		if (found == fItems.end() || id <= 3)
			return;
		found->second.view->RemoveSelf();
		delete found->second.view;
		fItems.erase(found);
	}

	void Close()
	{
		PostMessage(B_QUIT_REQUESTED);
	}

	// DispatchMessage: Escape shows Cancel pressed for a moment and
	// invokes it
	virtual void DispatchMessage(BMessage* message, BHandler* target)
	{
		const char* bytes;
		if (message->what == B_KEY_DOWN && message->FindString("bytes", &bytes) == B_OK
				&& bytes[0] == B_ESCAPE) {
			BButton* cancel = static_cast<BButton*>(fItems[2].view);
			cancel->SetValue(1);
			UpdateIfNeeded();
			snooze(100000);
			cancel->SetValue(0);
			UpdateIfNeeded();
			snooze(100000);
			cancel->Invoke();
			return;
		}
		BWindow::DispatchMessage(message, target);
	}

	virtual void MessageReceived(BMessage* message)
	{
		int32 id = int32(message->what - 'sD00');
		auto found = id >= 0 && id < 0x4a4a ? fItems.find(id) : fItems.end();
		if (found == fItems.end()) {
			BWindow::MessageReceived(message);
			return;
		}
		if (id == 3) {
			fRun->About();
			return;
		}
		Entry& item = found->second;
		long data[32 / sizeof(long)] = {};
		switch (item.type) {
			case kItemPushButton:
				// (OK with a text field being typed into: that first)
				if (id == 1)
					CommitText();
				break;
			case kItemRadioButton:
				// (the others of its group off)
				for (auto& [other, entry] : fItems) {
					if (entry.type == kItemRadioButton && entry.parent == item.parent
							&& other != id) {
						long off[32 / sizeof(long)] = {};
						fRun->SendCallback(fID, other, off, sizeof(off));
					}
				}
				data[0] = static_cast<BControl*>(item.view)->Value();
				break;
			case kItemCheckButton:
				data[0] = static_cast<BControl*>(item.view)->Value();
				break;
			case kItemScale: {
				// (the moves queued since: only the last)
				BMessage* next;
				while ((next = MessageQueue()->FindMessage(message->what, 0)) != NULL) {
					MessageQueue()->RemoveMessage(next);
					delete next;
				}
				TGIMPScale* scale = static_cast<TGIMPScale*>(item.view);
				void* source = NULL;
				message->FindPointer("source", &source);
				bool fromText = source == scale->Text();
				if (fromText && !scale->Valid())
					return;
				data[0] = scale->Value(fromText);
				break;
			}
			case kItemText:
				strncpy((char*)data, static_cast<BTextControl*>(item.view)->Text(), 31);
				break;
			default:
				return;
		}
		fRun->SendCallback(fID, id, data, sizeof(data));
	}

private:
	void CommitText()
	{
		BView* focus = CurrentFocus();
		if (dynamic_cast<BTextView*>(focus) == NULL)
			return;
		for (auto& [id, entry] : fItems) {
			BView* control = entry.type == kItemScale
				? static_cast<TGIMPScale*>(entry.view)->Text()
				: entry.type == kItemText ? entry.view : NULL;
			if (control == NULL || focus->Parent() != control)
				continue;
			BMessage typed('sD00' + uint32(id));
			typed.AddPointer("source", control);
			MessageReceived(&typed);
		}
	}

	struct Entry {
		int32 type;
		BView* view;
		BGroupLayout* layout;
		int32 parent;
		int32 precision = 0;
	};
	GIMPRun* fRun;
	int32 fID;
	BString fDocumentName;
	std::map<int32, Entry> fItems;
	int32 fNextItem = 4;
};


// #pragma mark - GIMPRun


GIMPRun::GIMPRun(BMessenger view, const ImagePlugin& plugin, const ArchiveBitmap& bits,
	IMRect selection, BWindow* window, const uint8 colors[2][4])
	:
	fView(view),
	fName(plugin.name),
	fPath(plugin.path),
	fBits(bits),
	fSelection(selection),
	fWindow(window),
	fWindowFrame(window != NULL ? window->Frame() : BRect(0, 0, 0, 0)),
	fDocumentName(window != NULL ? window->Title() : "")
{
	memcpy(fColors, colors, sizeof(fColors));
	GIMPInfo(plugin, fDescription, fAuthor, fEmail);
}


GIMPRun::~GIMPRun()
{
	// (still running: stopped, which ends its pipes and the thread's reads)
	status_t result;
	if (fTeam >= B_OK && !fQuit)
		kill_team(fTeam);
	if (fThread >= B_OK)
		wait_for_thread(fThread, &result);
	Close();
	if (fTeam >= B_OK)
		wait_for_thread(fTeam, &result);
	for (int i = 0; i < 2; i++)
		if (fAreas[i] >= B_OK)
			delete_area(fAreas[i]);
	for (auto& [id, dialog] : fDialogs)
		dialog->Close();
	if (fStatus != NULL)
		fStatus->Finish();
}


status_t
GIMPRun::Start()
{
	if (pipe(fToPlugin) != 0 || pipe(fFromPlugin) != 0 || pipe(fInfo) != 0)
		return B_ERROR;
	// "-gimp <read> <write> <info>": its ends of the pipes
	BString read, write, info;
	read << fToPlugin[0];
	write << fFromPlugin[1];
	info << fInfo[0];
	const char* argv[] = {fPath.String(), "-gimp", read.String(), write.String(), info.String(),
		NULL};
	fTeam = load_image(5, argv, (const char**)environ);
	if (fTeam < B_OK) {
		Close();
		return fTeam;
	}
	// (theirs closed here, so a filter that dies ends the reads)
	close(fToPlugin[0]);
	close(fFromPlugin[1]);
	close(fInfo[0]);
	fToPlugin[0] = fFromPlugin[1] = fInfo[0] = -1;
	fThread = spawn_thread(Thread, "GIMP proxy", B_NORMAL_PRIORITY, this);
	resume_thread(fThread);
	resume_thread(fTeam);
	return B_OK;
}


void
GIMPRun::Close()
{
	for (int* pair : {fToPlugin, fFromPlugin, fInfo})
		for (int i = 0; i < 2; i++)
			if (pair[i] >= 0) {
				close(pair[i]);
				pair[i] = -1;
			}
}


int32
GIMPRun::Thread(void* data)
{
	((GIMPRun*)data)->Run();
	return 0;
}


bool
GIMPRun::ReadAll(void* buffer, size_t size)
{
	uint8* at = (uint8*)buffer;
	while (size > 0) {
		ssize_t got = read(fFromPlugin[0], at, size);
		if (got <= 0)
			return false;
		at += got;
		size -= size_t(got);
	}
	return true;
}


void
GIMPRun::Reply(int32 type, size_t size, const void* data)
{
	std::lock_guard<std::mutex> lock(fWriteLock);
	MsgHeader header = {type, long(size)};
	if (write(fToPlugin[1], &header, sizeof(header)) < 0)
		return;
	if (size > 0)
		write(fToPlugin[1], data, size);
}


void
GIMPRun::Ack(int32 type, size_t size, const void* data)
{
	std::lock_guard<std::mutex> lock(fWriteLock);
	MsgHeader header = {type, long(size)};
	if (write(fInfo[1], &header, sizeof(header)) < 0)
		return;
	if (size > 0)
		write(fInfo[1], data, size);
}


// GIMPPlugin::GIMPThread: each message from the filter, answered, until it
// quits (or its pipe closes)
void
GIMPRun::Run()
{
	while (true) {
		MsgHeader header;
		if (!ReadAll(&header, sizeof(header)))
			break;
		std::vector<uint8> payload(size_t(std::max(0L, header.size)));
		if (!payload.empty() && !ReadAll(payload.data(), payload.size()))
			break;
		void* data = payload.empty() ? NULL : payload.data();
		switch (header.type) {
			case kQuit:
				fQuit = true;
				fView.SendMessage(kMsgDone);
				return;
			case kImageNew:
			case kImageInput:
			case kImageOutput:
				if (data != NULL)
					HandleImage(header.type, data);
				break;
			case kImageDisplay:
				Ack(header.type, 0, NULL);
				break;
			case kImageColor: {
				// handle_image_color: the foreground or background color
				if (data == NULL)
					break;
				MsgColor color = *(MsgColor*)data;
				int which = color.type == 0 ? 0 : 1;
				color.r = fColors[which][0];
				color.g = fColors[which][1];
				color.b = fColors[which][2];
				Ack(kImageColor, sizeof(color), &color);
				Reply(kImageColor, sizeof(color), &color);
				break;
			}
			case kImageUpdate:
				HandleUpdate(data);
				break;
			case kLoad:
			case kSave:
				Close();
				break;
			case kParams:
				if (data != NULL)
					HandleParams(data);
				break;
			case kProgress:
				HandleProgress(data);
				break;
			case kMessage:
				if (data != NULL)
					StringAlert((const char*)data);
				Ack(kMessage, 0, NULL);
				break;
			case kDialog:
				if (data != NULL)
					HandleDialog(data);
				break;
		}
	}
	fView.SendMessage(kMsgDone);
}


// the image packed as the filters take it (GIMPImage::GetPackedBits): red,
// green, blue (and alpha) a pixel, the image's rows one after the other
area_id
GIMPRun::PackedArea(bool output)
{
	int index = output ? 1 : 0;
	if (fAreas[index] >= B_OK)
		return fAreas[index];
	int width = fBits.Width(), height = fBits.Height();
	int channels = fBits.colorSpace == 0x0008 ? 3 : 4;
	size_t size = size_t(width) * size_t(height) * size_t(channels);
	size = (size + B_PAGE_SIZE - 1) / B_PAGE_SIZE * B_PAGE_SIZE;
	void* address = NULL;
	area_id area = create_area(output ? "GIMP output" : "GIMP input", &address, B_ANY_ADDRESS,
		size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (area < B_OK)
		return area;
	uint8* to = (uint8*)address;
	for (int y = 0; y < height; y++) {
		const uint8* from = &fBits.bits[size_t(y) * fBits.bytesPerRow];
		for (int x = 0; x < width; x++, from += 4) {
			*to++ = from[2];
			*to++ = from[1];
			*to++ = from[0];
			if (channels == 4)
				*to++ = from[3];
		}
	}
	fAreas[index] = area;
	fAreaBits[index] = address;
	return area;
}


// handle_image_input, handle_image_output (handle_image_new: another image
// the image's size): the image's name, size, channels, the selection's
// rectangle (its right and bottom past it) and the area
void
GIMPRun::HandleImage(int32 type, void* data)
{
	MsgImage image = *(MsgImage*)data;
	bool output = type != kImageInput;
	area_id area = PackedArea(output);
	if (area < B_OK) {
		Close();
		return;
	}
	strlcpy(image.name, fDocumentName.String(), sizeof(image.name));
	if (type == kImageInput && image.ID == 0)
		image.ID = 1;
	if (type != kImageInput)
		image.ID = 2;
	image.width = fBits.Width();
	image.height = fBits.Height();
	image.type = kTypeRGB;
	image.channels = fBits.colorSpace == 0x0008 ? 3 : 4;
	image.colors = 0;
	image.shmid = area;
	image.x1 = fSelection.left;
	image.y1 = fSelection.top;
	image.x2 = fSelection.right + 1;
	image.y2 = fSelection.bottom + 1;
	Ack(type, sizeof(image), &image);
	Reply(type, sizeof(image), &image);
}


// handle_image_update: the output unpacked (GIMPImage::UnpackBits) and shown
void
GIMPRun::HandleUpdate(void* data)
{
	Ack(kImageUpdate, 0, NULL);
	if (data == NULL || fAreaBits[1] == NULL || ((MsgImage*)data)->ID != 2)
		return;
	ArchiveBitmap* result = new ArchiveBitmap(fBits);
	int channels = fBits.colorSpace == 0x0008 ? 3 : 4;
	const uint8* from = (const uint8*)fAreaBits[1];
	for (int y = 0; y < result->Height(); y++) {
		uint8* to = &result->bits[size_t(y) * result->bytesPerRow];
		for (int x = 0; x < result->Width(); x++, to += 4, from += channels) {
			to[0] = from[2];
			to[1] = from[1];
			to[2] = from[0];
			if (channels == 4)
				to[3] = from[3];
		}
	}
	BMessage update(kMsgUpdate);
	update.AddPointer("bits", result);
	if (fView.SendMessage(&update) != B_OK)
		delete result;
}


// handle_params: the filter's settings from last time, or kept for next
void
GIMPRun::HandleParams(void* data)
{
	MsgParams params = *(MsgParams*)data;
	std::lock_guard<std::mutex> lock(sParamsLock);
	BMessage store;
	BFile file(ParamsPath().Path(), B_READ_ONLY);
	if (file.InitCheck() == B_OK)
		store.Unflatten(&file);
	if (params.type == kParamsGet) {
		const void* saved = NULL;
		ssize_t size = 0;
		if (store.FindData(fName.String(), B_RAW_TYPE, &saved, &size) != B_OK)
			size = 0;
		params.size = size;
		params.data = NULL;
		Ack(kParams, sizeof(params), &params);
		std::lock_guard<std::mutex> writing(fWriteLock);
		MsgHeader header = {kParams, long(sizeof(params))};
		write(fToPlugin[1], &header, sizeof(header));
		write(fToPlugin[1], &params, sizeof(params));
		if (size > 0)
			write(fToPlugin[1], saved, size_t(size));
		return;
	}
	std::vector<uint8> value(size_t(std::max(0L, params.size)));
	if (!value.empty() && !ReadAll(value.data(), value.size()))
		return;
	store.RemoveName(fName.String());
	store.AddData(fName.String(), B_RAW_TYPE, value.data(), ssize_t(value.size()));
	BFile out(ParamsPath().Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (out.InitCheck() == B_OK)
		store.Flatten(&out);
	Ack(kParams, sizeof(params), &params);
}


// handle_progress: the Status window, its bar at current over max
void
GIMPRun::HandleProgress(void* data)
{
	Ack(kProgress, 0, NULL);
	if (data == NULL)
		return;
	const MsgProgress* progress = (const MsgProgress*)data;
	if (fStatus == NULL) {
		fStatus = new TStatusDialog(fName.String(), NULL, NULL);
		fStatus->RunDialog();
	}
	if (progress->max > 0)
		fStatus->Update(float(progress->current) / float(progress->max));
}


// handle_dialog
void
GIMPRun::HandleDialog(void* data)
{
	MsgDialog dialog = *(MsgDialog*)data;
	switch (dialog.type) {
		case kDialogNew: {
			int32 id = fNextDialog++;
			fDialogs[id] = new TGIMPDialog(this, id, dialog.dialog.title, fWindow, fWindowFrame,
				fDocumentName.String(),
				fDescription.Length() > 0 || fAuthor.Length() > 0 || fEmail.Length() > 0);
			dialog.dialog.dialog_ID = id;
			Ack(kDialog, sizeof(dialog), &dialog);
			Reply(kDialog, sizeof(dialog), &dialog);
			return;
		}
		case kDialogShow: {
			auto found = fDialogs.find(dialog.dialog.dialog_ID);
			if (found != fDialogs.end())
				found->second->Show();
			Ack(kDialog, sizeof(dialog), &dialog);
			Reply(kDialog, sizeof(dialog), &dialog);
			return;
		}
		case kDialogClose: {
			auto found = fDialogs.find(dialog.dialog.dialog_ID);
			if (found != fDialogs.end()) {
				found->second->Close();
				fDialogs.erase(found);
			}
			Ack(kDialog, sizeof(dialog), &dialog);
			return;
		}
		case kDialogNewItem: {
			auto found = fDialogs.find(dialog.item.dialog_ID);
			dialog.item.item_ID = found != fDialogs.end()
				? found->second->AddItem(dialog.item.parent_ID, dialog.item.item_type,
					dialog.item.data)
				: -1;
			Ack(kDialog, sizeof(dialog), &dialog);
			Reply(kDialog, sizeof(dialog), &dialog);
			return;
		}
		case kDialogChangeItem:
		case kDialogShowItem:
		case kDialogHideItem:
		case kDialogDeleteItem: {
			auto found = fDialogs.find(dialog.item.dialog_ID);
			if (found != fDialogs.end()) {
				if (dialog.type == kDialogChangeItem)
					found->second->ChangeItem(dialog.item.item_ID, dialog.item.data);
				else if (dialog.type == kDialogDeleteItem)
					found->second->DeleteItem(dialog.item.item_ID);
				else
					found->second->ShowItem(dialog.item.item_ID, dialog.type == kDialogShowItem);
			}
			Ack(kDialog, sizeof(dialog), &dialog);
			return;
		}
		default:
			Ack(kDialog, sizeof(dialog), &dialog);
			return;
	}
}


void
GIMPRun::SendCallback(int32 dialog, int32 item, const void* data, size_t size)
{
	MsgDialog message = {};
	message.callback.type = kDialogCallback;
	message.callback.dialog_ID = dialog;
	message.callback.item_ID = item;
	memcpy(message.callback.data, data, std::min(size, sizeof(message.callback.data)));
	Reply(kDialog, sizeof(message), &message);
}


void
GIMPRun::About()
{
	ShowAbout(fName, fDescription, fAuthor, fEmail);
}

} // namespace gobe
