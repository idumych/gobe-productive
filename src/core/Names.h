// Named selections (Name Selection…, TNameSelectionCommand): a selection
// kept under a name in the part's references, which formulas and Go To
// Cell… can use.
#ifndef GOBE_NAMES_H
#define GOBE_NAMES_H

#include <string>
#include <vector>

#include "SSPart.h"

namespace gobe {

struct Document;

// TString::ValidName: 1 to 64 characters, a letter first, then letters,
// digits, spaces, '_' and '.'; not a digit right after the first one or two
// letters (it would read as a cell, "A1", "AB12")
bool ValidName(const std::string& name);

// TNameSelectionCommand::Perform for cells: a copy of the selection (its
// ranges and active cell, a TSSSelection 'CELL') added to the document and
// a TReference to it under `name` in part `part`'s references (TRefTable::
// AddReference: the table's next id, its index). False, nothing changed,
// if a reference of that name exists (TDoc::FindReference) or the name
// isn't valid.
bool NameCellSelection(Document& doc, size_t part, const std::string& name,
	const std::vector<SSRange>& ranges, CellID active);

// TDeleteNamedSelectionCommand::Perform: the name's reference marked dead
// (TReference::SetDead), so formulas no longer find it and the name can be
// given again. False if no live reference has that name.
bool DeleteName(Document& doc, const std::string& name);

// TRenameNamedSelectionCommand::Perform: the reference renamed. Formulas
// using the old name aren't changed (nor in the original). False, nothing
// changed, if `name` isn't a name or `newName` is taken or not valid.
bool RenameName(Document& doc, const std::string& name, const std::string& newName);

// TSSSelection::Description for a named cell selection: its ranges,
// "A1..B2,C3"; "" if `name` names no cells
std::string NameDescription(const Document& doc, const std::string& name);

// the names of the document's selections (TSelectionMgr::
// GetNamedSelections), in the parts' order
std::vector<std::string> SelectionNames(const Document& doc);

} // namespace gobe

#endif
