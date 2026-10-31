// Named selections: TNameSelectionCommand (Gobe Productive/
// TNameSelectionCommand.c), TReference, TRefTable::AddReference, TString::
// ValidName, TSSSelection::Write.
#include "Names.h"

#include "DocWriter.h"
#include "Document.h"

namespace gobe {

bool
ValidName(const std::string& name)
{
	auto letter = [](unsigned char c) {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80;
	};
	auto digit = [](unsigned char c) { return c >= '0' && c <= '9'; };
	size_t characters = 0;
	for (unsigned char c : name)
		characters += (c & 0xc0) != 0x80;
	if (name.empty() || characters > 64 || !letter((unsigned char)name[0]))
		return false;
	for (unsigned char c : name)
		if ((c & 0xc0) != 0x80 && c != ' ' && c != '_' && c != '.' && !letter(c) && !digit(c))
			return false;
	// a digit after one or two letters reads as a cell
	int letters = 0;
	for (unsigned char c : name) {
		if ((c & 0xc0) == 0x80)
			continue;
		if (digit(c) && (letters == 1 || letters == 2))
			return false;
		if (!letter(c))
			return true;
		if (++letters > 2)
			return true;
	}
	return true;
}


bool
NameCellSelection(Document& doc, size_t part, const std::string& name,
	const std::vector<SSRange>& ranges, CellID active)
{
	if (!ValidName(name) || ranges.empty() || part >= doc.tails.size() || !doc.tailRead[part])
		return false;
	size_t foundPart, foundRef;
	if (doc.FindName(name, -1, foundPart, foundRef))
		return false;
	// the selection's record (TSelectionMgr: its size, then TSelection::
	// Write — the part's kind, the part, its id — and TSSSelection::Write)
	uint32_t id = uint32_t(doc.selections.selections.size());
	DocWriter body;
	body.Sig(Sig("SPRD"));
	body.UInt32(uint32_t(part));
	body.UInt32(id);
	body.Sig(Sig("CELL"));
	body.UInt16(active.a);
	body.UInt16(active.b);
	body.UInt32(uint32_t(ranges.size()));
	for (const SSRange& r : ranges) {
		body.UInt16(r.from.a);
		body.UInt16(r.from.b);
		body.UInt16(r.to.a);
		body.UInt16(r.to.b);
	}
	body.Bool(true);
	DocWriter record;
	record.UInt32(uint32_t(body.Data().size()));
	record.Bytes(body.Data());
	doc.addedSelections.push_back(record.Data());
	Selection s;
	s.id = id;
	s.kind = Sig("SPRD");
	s.part = uint32_t(part);
	s.value = id;
	s.cell[0] = active.a;
	s.cell[1] = active.b;
	for (const SSRange& r : ranges)
		s.ranges.push_back({r.from.a, r.from.b, r.to.a, r.to.b});
	s.hasCell = true;
	doc.selections.selections.push_back(s);
	// the reference (TReference: not dirty, not dead, one user)
	PartTail& tail = doc.tails[part];
	Reference ref;
	ref.value14 = tail.refNext++;
	ref.name = name;
	ref.selection = id;
	ref.valueC = uint32_t(tail.refs.size());
	ref.value10 = 1;
	tail.refs.push_back(ref);
	tail.refsChanged = true;
	doc.dirtyTails.insert(part);
	return true;
}


std::vector<std::string>
SelectionNames(const Document& doc)
{
	std::vector<std::string> names;
	for (size_t part = 0; part < doc.tails.size(); part++) {
		if (!doc.tailRead[part])
			continue;
		for (const Reference& r : doc.tails[part].refs)
			if (!r.name.empty() && !r.flag1 && r.selection != 0xffffffff)
				names.push_back(r.name);
	}
	return names;
}

} // namespace gobe
