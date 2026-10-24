// Undo and redo of edits, kept in memory apart from the file (Gobe's own
// CommandMgr section is copied unchanged on save). Each step is a snapshot
// of what an edit can change: the edited part's parsed data, the document's
// links (UpdateDependencies adds some), the styles (text styling adds
// some) and which parts were dirty, or for structural changes the whole
// document. Undo
// swaps the snapshot with the current state, so redo is the same operation
// the other way.
#ifndef GOBE_UNDO_H
#define GOBE_UNDO_H

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "Document.h"

namespace gobe {

class UndoStack {
public:
	// what the view restores with a step (caret, selection); opaque here
	typedef std::vector<int64_t> ViewState;

	UndoStack();
	~UndoStack();

	// Call before changing `part`. Steps with the same nonzero `merge` key
	// in a row (typing) become one, until Break().
	void Begin(const Document& doc, size_t part, const std::string& label,
		const ViewState& view, uint64_t merge = 0);
	// Call before changing the document's structure (a part added, the
	// pages, the sheet): the step keeps the whole document.
	void BeginDocument(const Document& doc, const std::string& label, const ViewState& view);
	// the next Begin starts a new step (the caret moved, ...)
	void Break() { fMerge = 0; }

	bool CanUndo() const { return !fUndo.empty(); }
	bool CanRedo() const { return !fRedo.empty(); }
	// the label of the step Undo/Redo would apply ("" if none)
	std::string UndoLabel() const;
	std::string RedoLabel() const;

	// `view` is the view's state now; on success it's replaced with the
	// state to restore. Returns the part changed, kDocumentChanged for a
	// BeginDocument step, or -1.
	int Undo(Document& doc, ViewState& view);
	int Redo(Document& doc, ViewState& view);

	// After saving, the file differs from what was loaded: undone parts
	// stay dirty from now on.
	void DocumentSaved();
	// after revert or reload
	void Clear();

	static const size_t kMaxSteps = 100;
	static const int kDocumentChanged = -2;
	// THistory::SetMaxSize: the steps kept (0: all)
	void SetLimit(size_t steps);
	size_t Limit() const { return fLimit; }
	// the steps' labels, the oldest first (those undone after the
	// others), and how many are done
	std::vector<std::string> Labels(size_t& done) const;

private:
	struct Step;
	int Swap(Document& doc, std::vector<std::unique_ptr<Step>>& from,
		std::vector<std::unique_ptr<Step>>& to, ViewState& view);

	std::vector<std::unique_ptr<Step>> fUndo, fRedo;
	uint64_t fMerge = 0;
	size_t fLimit = kMaxSteps;
};

} // namespace gobe

#endif
