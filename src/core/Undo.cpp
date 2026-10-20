#include "Undo.h"

#include <optional>

namespace gobe {

struct UndoStack::Step {
	size_t part = 0;
	std::string label;
	std::optional<WPPart> word;
	std::optional<SSPart> sheet;
	LinksMgr links;
	StyleMgr styles;
	bool stylesChanged = false;
	bool wasDirty = false;
	bool trustDirty = true;			// false once the document was saved
	ViewState view;

	// the document's state of the step's part
	void Take(const Document& doc)
	{
		word.reset();
		sheet.reset();
		if (const WPPart* wp = doc.WordPart(part))
			word = *wp;
		else if (const SSPart* ss = doc.SpreadsheetPart(part))
			sheet = *ss;
		links = doc.links;
		styles = doc.styles;
		stylesChanged = doc.stylesChanged;
		wasDirty = doc.dirtyParts.count(part) != 0;
	}

	// exchanges the snapshot with the document's state
	void Exchange(Document& doc)
	{
		bool dirty = doc.dirtyParts.count(part) != 0;
		if (word) {
			WPPart* wp = doc.EditableWordPart(part);
			if (wp != nullptr)
				std::swap(*wp, *word);
		} else if (sheet) {
			SSPart* ss = doc.EditableSpreadsheetPart(part);
			if (ss != nullptr)
				std::swap(*ss, *sheet);
		}
		std::swap(doc.links, links);
		std::swap(doc.styles, styles);
		// once saved, the file's StyleMgr may differ from either
		bool changed = doc.stylesChanged;
		doc.stylesChanged = trustDirty ? stylesChanged : true;
		stylesChanged = changed;
		if (!wasDirty && trustDirty)
			doc.dirtyParts.erase(part);
		else
			doc.dirtyParts.insert(part);
		wasDirty = dirty;
	}
};


UndoStack::UndoStack() = default;
UndoStack::~UndoStack() = default;


void
UndoStack::Begin(const Document& doc, size_t part, const std::string& label,
	const ViewState& view, uint64_t merge)
{
	fRedo.clear();
	if (merge != 0 && merge == fMerge && !fUndo.empty() && fUndo.back()->part == part)
		return;
	fMerge = merge;
	auto step = std::make_unique<Step>();
	step->part = part;
	step->label = label;
	step->view = view;
	step->Take(doc);
	fUndo.push_back(std::move(step));
	if (fUndo.size() > kMaxSteps)
		fUndo.erase(fUndo.begin());
}


std::string
UndoStack::UndoLabel() const
{
	return fUndo.empty() ? std::string() : fUndo.back()->label;
}


std::string
UndoStack::RedoLabel() const
{
	return fRedo.empty() ? std::string() : fRedo.back()->label;
}


int
UndoStack::Swap(Document& doc, std::vector<std::unique_ptr<Step>>& from,
	std::vector<std::unique_ptr<Step>>& to, ViewState& view)
{
	if (from.empty())
		return -1;
	fMerge = 0;
	std::unique_ptr<Step> step = std::move(from.back());
	from.pop_back();
	step->Exchange(doc);
	std::swap(step->view, view);
	int part = int(step->part);
	to.push_back(std::move(step));
	return part;
}


int
UndoStack::Undo(Document& doc, ViewState& view)
{
	return Swap(doc, fUndo, fRedo, view);
}


int
UndoStack::Redo(Document& doc, ViewState& view)
{
	return Swap(doc, fRedo, fUndo, view);
}


void
UndoStack::DocumentSaved()
{
	for (auto* list : {&fUndo, &fRedo})
		for (auto& step : *list)
			step->trustDirty = false;
	fMerge = 0;
}


void
UndoStack::Clear()
{
	fUndo.clear();
	fRedo.clear();
	fMerge = 0;
}

} // namespace gobe
