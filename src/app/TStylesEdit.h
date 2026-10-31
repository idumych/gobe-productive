// Format > Edit Styles… (TDialogStylesEdit, DIAL/Item 10, a floater with
// MENU 210-212) and Style > Modify Style… (TDialogStyleModify, DIAL/Item
// 12).
#ifndef GOBE_TSTYLESEDIT_H
#define GOBE_TSTYLESEDIT_H

#include <Message.h>

#include <map>
#include <string>
#include <vector>

#include "DocSections.h"
#include "StyleArchive.h"
#include "StyleEdit.h"
#include "StyleText.h"
#include "TDialog.h"

class BListView;

class BWindow;
class BOutlineListView;

namespace gobe {

struct Document;
class TMenu;

// what the floater shows of the front document window, taken in its thread
struct StylesSnapshot {
	const Document* document = NULL;
	StyleMgr styles;
	// TPart::ActiveStyles: the active part's style types (none: it isn't
	// text)
	std::vector<uint32> types;
	SelectionStyles selection;
};

// TApp +0x188: the styles Edit > Copy Style keeps for Paste Style
void SetStyleClipboard(const std::vector<StyleDefinition>& styles);
std::vector<StyleDefinition> StyleClipboard();

// TDialogStylesEdit: Available Styles (1, TStyleListView: each kind's
// named styles under a heading, the selection's marked) and the chosen
// style's properties (3: its base, its own, inherited and, with "+", the
// selection's own). Its menus' commands go to the front window with the
// style ("Target Style"); double-clicking one modifies it. While a style
// is chosen, format commands change it (TDoc::EditingStyle).
class TDialogStylesEdit : public TDialog {
public:
	// with "Name": that style chosen first
	TDialogStylesEdit(const char* name);
	virtual ~TDialogStylesEdit();

	static TDialogStylesEdit* Open();
	// TFloaterMgr::UpdateFloater: the open floater's contents again
	static void Refresh();
	// TDoc::EditingStyle: the style chosen for `document` ("" none)
	static std::string EditingStyle(const Document* document);
	// test hooks: row `row` (of the whole list) chosen; a menu command;
	// the rows and the properties printed
	static void ChooseRow(int32 row, bool invoke = false);
	static void Command(uint32 command);
	static void Dump();

	virtual void MessageReceived(BMessage* message);
	virtual void MenusBeginning();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	struct Row {
		std::string label;
		uint32 type = 0;
		int32 style = -1;			// -1: a heading
		uint32 flags = 0;			// TStyleListItem: 1 heading, 2 applied to
									// part, 4 applied, 8 the active style
	};

	// TDialogStylesEdit::UpdateContents
	void Update(bool force);
	// TStyleListView::GetStyles, BuildList
	void GetStyles();
	// TDialogStylesEdit::FindStyleByName: its row (selected), or -1
	int32 FindStyleByName(const std::string& name);
	void ReloadProperties(int32 row);
	int32 SelectedRow() const;
	void SetEditing();

	BOutlineListView* fStyles = NULL;
	BListView* fProperties = NULL;
	TMenu* fFile = NULL;
	TMenu* fEdit = NULL;
	TMenu* fStyle = NULL;
	StylesSnapshot fSnapshot;
	bool fHaveSnapshot = false;
	std::vector<Row> fRows;
	std::map<std::string, bool> fExpanded;		// the headings'
	std::string fName;							// +0x5c
	bool fReloadStyles = true;					// +0x60
};

// TDialogStyleModify: Style Name (2) and Based On (3, TStyleManager::
// FillBaseStyleMenu: none, then each kind's named styles that don't come
// from it, its base marked). OK checks the name (STR# 6, 36) and sends
// `window` 'sMB7' with "Target Style", "Style Name" and "Style Base" if
// either changed.
class TDialogStyleModify : public TDialog {
public:
	TDialogStyleModify(BLooper* window, const std::string& name, const StyleMgr& styles,
		const std::vector<uint32>& types);

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	BLooper* fWindow;
	std::string fName, fBase;
	StyleMgr fStyles;
	std::vector<uint32> fTypes;
};

// TDialogStyleReplaceRename (DIAL/Item 13): a style pasted has a style's
// name: Rename (0, with New name, 5: on when it's another name), Cancel
// (1), Replace (2) or Skip (3).
class TDialogStyleReplaceRename : public TDialog {
public:
	// TDialog::BlockingRun: the button chosen (closed: Cancel), `window`
	// drawn meanwhile
	static int32 Ask(BWindow* window, const std::string& name, std::string& newName);

	virtual ~TDialogStyleReplaceRename();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	TDialogStyleReplaceRename(const std::string& name);

	std::string fName;
	std::string fNewName;
	int32 fResult = 1;
	int32* fResultOut = NULL;
	std::string* fNewNameOut = NULL;
	sem_id fDone = -1;
};

// TDialogPickStyles (DIAL/Item 70): Export Styles' and Import Styles'
// choice of styles: a list of names with check boxes (5), Select All (2)
// and Select None (3), Replace all styles with the same name (6, import
// only, on at first). True on OK, with `checked` and `replace` updated.
class TDialogPickStyles : public TDialog {
public:
	static bool Ask(BWindow* window, bool import, const std::vector<std::string>& names,
		std::vector<bool>& checked, bool& replace);

	virtual ~TDialogPickStyles();

protected:
	virtual void Init();
	virtual bool ItemHit(int32 index, BMessage* message);

private:
	TDialogPickStyles(bool import, const std::vector<std::string>& names,
		const std::vector<bool>& checked);

	bool fImport;
	std::vector<std::string> fNames;
	std::vector<bool> fChecked;
	bool fReplace = true;
	bool fOK = false;
	bool* fOKOut = NULL;
	std::vector<bool>* fCheckedOut = NULL;
	bool* fReplaceOut = NULL;
	sem_id fDone = -1;
};

// The styles file's message as a BMessage and back (StyleArchive's trees)
BMessage ArchiveToMessage(const ArchiveMessage& archive);
ArchiveMessage MessageToArchive(const BMessage& message);

} // namespace gobe

#endif
