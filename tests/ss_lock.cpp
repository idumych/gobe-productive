// LockCells and CellsLocked: Format > Lock Cells and Unlock Cells as
// TSSLockCommand::Perform does them (decomp/Spreadsheet/TSSLockCommand.c,
// TCellsList::AddList/AddRange without merging, RemoveRange: decomp/Gobe
// Productive/TCellsList.c). Expected lists worked out by hand from that code.
// Also: the locked cells and Auto Calculate are written (WriteSSPart) and
// read back.
#include <cstdio>
#include <string>

#include "Document.h"
#include "DocWriter.h"
#include "Editing.h"

using namespace gobe;

static int ok = 0, failed = 0;

static void
Check(bool cond, const std::string& desc)
{
	if (cond) {
		ok++;
		return;
	}
	failed++;
	if (failed <= 20)
		printf("FAIL: %s\n", desc.c_str());
}


static std::string
List(const SSPart& part)
{
	std::string s;
	for (const SSRange& r : part.locked)
		s += std::to_string(r.from.a) + "," + std::to_string(r.from.b) + "-"
			+ std::to_string(r.to.a) + "," + std::to_string(r.to.b) + " ";
	return s;
}


int
main(int argc, char** argv)
{
	// the list operations on a part of its own
	SSPart part;
	part.used = {{0, 0}, {100, 100}};
	// locking appends, normalized, without merging (even what's already in)
	LockCells(part, 5, 6, 2, 3, true);
	Check(List(part) == "2,3-5,6 ", "lock a block: " + List(part));
	LockCells(part, 2, 3, 5, 6, true);
	Check(List(part) == "2,3-5,6 2,3-5,6 ", "lock it again: " + List(part));
	Check(CellsLocked(part, 5, 6, 9, 9), "a block touching it is locked");
	Check(!CellsLocked(part, 6, 0, 9, 9), "a block below it isn't");
	// unlocking the middle cell of 1,1-3,3: it's split into above (1,1-1,3),
	// then left of the rest (2,1-3,1), below the rest (3,2-3,3), right (2,3-2,3)
	part.locked.clear();
	LockCells(part, 1, 1, 3, 3, true);
	Check(LockCells(part, 2, 2, 2, 2, false), "unlock the middle reports a change");
	Check(List(part) == "1,1-1,3 2,1-3,1 3,2-3,3 2,3-2,3 ", "unlock the middle: " + List(part));
	Check(!CellsLocked(part, 2, 2, 2, 2), "the middle isn't locked");
	Check(CellsLocked(part, 2, 3, 2, 3), "its right neighbour is");
	// unlocking a block covering a whole range just removes it
	part.locked.clear();
	LockCells(part, 1, 1, 2, 2, true);
	LockCells(part, 10, 10, 11, 11, true);
	LockCells(part, 0, 0, 5, 5, false);
	Check(List(part) == "10,10-11,11 ", "unlock around a range: " + List(part));
	// unlocking what isn't locked changes nothing
	Check(!LockCells(part, 50, 50, 60, 60, false), "unlock elsewhere changes nothing");
	// two ranges cut by one unlock: the stack gives them back last first
	part.locked.clear();
	LockCells(part, 0, 0, 0, 4, true);	// a row
	LockCells(part, 0, 2, 4, 2, true);	// a column through it
	LockCells(part, 0, 2, 0, 2, false);	// their common cell
	Check(List(part) == "1,2-4,2 0,0-0,1 0,3-0,4 ",
		"unlock where two cross: " + List(part));

	// FillTargets: the block without the source row (column)
	uint16_t r0 = 2, c0 = 3, r1 = 5, c1 = 6;
	Check(FillTargets(r0, c0, r1, c1, FillKind::up) && r1 == 4 && r0 == 2,
		"Fill Up's targets end a row higher");
	r0 = 0; c0 = 0; r1 = 0; c1 = 0;
	Check(!FillTargets(r0, c0, r1, c1, FillKind::up), "a one-cell block has no targets");

	// written and read back, in every spreadsheet part of the documents
	int parts = 0;
	for (int i = 1; i < argc; i++) {
		Document doc;
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		for (auto& [p, ss] : doc.spreadsheetParts) {
			SSPart changed = ss;
			LockCells(changed, 1, 2, 3, 4, true);
			changed.autoCalculate = !ss.autoCalculate;
			DocChanges changes;
			changes.parts[p] = WriteSSPart(changed, doc.parts.parts[p].data);
			Document saved;
			std::string name = std::string(argv[i]) + ": part " + std::to_string(p);
			if (!LoadDocumentBytes(SaveDocument(doc, changes), saved, error)) {
				Check(false, name + ": saved document doesn't load: " + error);
				continue;
			}
			const SSPart* back = saved.SpreadsheetPart(p);
			if (back == nullptr) {
				Check(false, name + " doesn't read back");
				continue;
			}
			parts++;
			Check(List(*back) == List(changed), name + ": locked cells read back as "
				+ List(*back));
			Check(back->autoCalculate == changed.autoCalculate, name
				+ ": Auto Calculate doesn't read back");
			Check(back->cells.size() == ss.cells.size(), name + ": cells changed");
		}
	}
	printf("parts %d, ok %d, failed %d\n", parts, ok, failed);
	return failed == 0 && parts > 0 ? 0 : 1;
}
