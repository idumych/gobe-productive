// Copying and pasting a table's cells (TTable::CopySelection, TTable::Paste,
// TTable::PasteCell).
#include "TableClip.h"

#include <algorithm>
#include <map>

#include "Document.h"
#include "InlineParts.h"
#include "TablePart.h"

namespace gobe {

TableClip
CopyTableCells(const Document& doc, size_t table, const TableEdit& edit,
	const TableSelection& selection)
{
	TableClip clip;
	const TablePart* t = doc.Table(table);
	if (t == nullptr)
		return clip;
	const WPPart* wp = doc.WordPart(t->textPart);
	std::vector<const TableEditCell*> selected = edit.Selected(selection);
	// a row of the copy for each row with cells selected (in the rows'
	// order, as Selected gives them)
	int at = -1;
	for (const TableEditCell* c : selected) {
		if (c->row != at) {
			clip.rows.emplace_back();
			at = c->row;
		}
		TableClipRow& row = clip.rows.back();
		TableClipCell copy;
		copy.width = c->width;
		copy.height = c->height;
		const TableEditText& text = edit.texts[size_t(c->id)];
		size_t first = TableEditParagraph(edit, c->id);
		size_t count = text.paragraphs.size();
		if (wp != nullptr && count > 0 && first + count <= wp->paragraphs.size()) {
			size_t last = first + count - 1;
			copy.text = CopyFragment(doc, t->textPart, {first, 0},
				{last, EditableLength(wp->paragraphs[last])});
		}
		copy.frame = CharacterFormat(doc.styles, text.frame.style);
		std::vector<Property> paragraph = ParagraphFormat(doc.styles, text.frame.style);
		copy.frame.insert(copy.frame.end(), paragraph.begin(), paragraph.end());
		// (TRow::SetHeight: the row as high as its highest cell)
		row.height = std::max(row.height, copy.height);
		row.cells.push_back(std::move(copy));
	}
	return clip;
}


bool
PasteTableCells(Document& doc, size_t table, int row, int column, const TableClip& clip,
	std::string& error)
{
	TableEdit edit;
	const TablePart* t = doc.Table(table);
	if (t == nullptr || !LoadTableEdit(doc, table, edit)) {
		error = "not a table";
		return false;
	}
	size_t textPart = t->textPart;
	// the cells pasted into: their text but its break gone (TWPPart::
	// MoveText), their frames styled as the copied ones'
	std::map<int, const TableClipCell*> pasted;	// by cell id
	for (size_t i = 0; i < clip.rows.size(); i++) {
		size_t r = size_t(row) + i;
		if (r >= edit.rows.size())
			break;
		for (size_t j = 0; j < clip.rows[i].cells.size(); j++) {
			size_t c = size_t(column) + j;
			if (c >= edit.rows[r].cells.size())
				break;
			const TableClipCell& from = clip.rows[i].cells[j];
			int id = edit.rows[r].cells[c].id;
			TableEditText& text = edit.texts[size_t(id)];
			if (!text.paragraphs.empty()) {
				WPParagraph kept = text.paragraphs.front();
				kept.text = "\x05";
				kept.misc.clear();
				if (kept.runs.size() > 1)
					kept.runs.resize(1);
				text.paragraphs = {kept};
			}
			uint32_t style = text.frame.style;
			for (const Property& p : from.frame)
				style = StyleWithProperty(doc.styles, style, p);
			text.frame.style = style;
			pasted[id] = &from;
		}
	}
	if (pasted.empty()) {
		error = "no cells to paste into";
		return false;
	}
	if (!StoreTableEdit(doc, table, edit, error))
		return false;
	// then the copied text where each emptied cell's starts (TWPPart::
	// CopyText), the last cell's first so the others' places hold
	if (!LoadTableEdit(doc, table, edit)) {
		error = "the table doesn't read back";
		return false;
	}
	std::vector<std::pair<size_t, const TableClipCell*>> places;
	for (const auto& [id, from] : pasted)
		places.push_back({TableEditParagraph(edit, id), from});
	std::sort(places.begin(), places.end(),
		[](const auto& a, const auto& b) { return a.first > b.first; });
	for (const auto& [paragraph, from] : places)
		if (!from->text.text.empty())
			PasteFragment(doc, textPart, {paragraph, 0}, from->text);
	doc.dirtyParts.insert(textPart);
	return true;
}


int
PasteTableClip(Document& doc, size_t container, TextPosition at, const TableClip& clip,
	std::string& error)
{
	if (clip.Empty()) {
		error = "nothing copied";
		return -1;
	}
	// the columns as wide as the cells of the row with the most
	size_t widest = 0;
	for (size_t i = 0; i < clip.rows.size(); i++)
		if (clip.rows[i].cells.size() > clip.rows[widest].cells.size())
			widest = i;
	NewTable spec = MakeNewTable(clip.rows.size(), clip.rows[widest].cells.size());
	for (size_t j = 0; j < spec.columns.size(); j++)
		spec.columns[j] = clip.rows[widest].cells[j].width;
	for (size_t i = 0; i < spec.rows.size(); i++)
		spec.rows[i] = std::max(17.0f, clip.rows[i].height);
	size_t text = 0;
	int part = AddTablePart(doc, container, spec, text, error);
	if (part < 0)
		return -1;
	InsertInlineFrame(doc, container, at, size_t(part));
	if (!PasteTableCells(doc, size_t(part), 0, 0, clip, error))
		return -1;
	return part;
}

} // namespace gobe
