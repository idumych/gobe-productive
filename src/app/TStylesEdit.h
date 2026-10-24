// Format > Edit Styles… (TDialogStylesEdit, DIAL/Item 10, a floater with
// MENU 210-212) and Style > Modify Style… (TDialogStyleModify, DIAL/Item
// 12).
#ifndef GOBE_TSTYLESEDIT_H
#define GOBE_TSTYLESEDIT_H

#include <map>
#include <string>
#include <vector>

#include "DocSections.h"
#include "StyleText.h"
#include "TDialog.h"

class BListView;
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

} // namespace gobe

#endif
