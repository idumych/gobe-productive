// Table and image parts made in a document (see InlineParts.h), written
// as TTablePart::Write, TPartIM::Write and the parts' tails (TPortal,
// TFrame and its subclasses) lay them out.
#include "InlineParts.h"

#include <algorithm>

#include <zlib.h>

#include "BlankParts.h"
#include "DocWriter.h"
#include "Document.h"
#include "PartTail.h"
#include "TablePart.h"
#include "WPPart.h"

namespace gobe {

namespace {

const uint32_t kNone = 0xffffffff;


// the versions the document's records are read with
DocArchive
Versions(const Document& doc)
{
	DocArchive versions(std::vector<uint8_t>{});
	versions.SetVersions(doc.file.header.version.values[0], doc.file.header.version.values[3],
		doc.file.header.version.values[2]);
	return versions;
}


// TPart::TPart: the next part id (the highest so far + 1)
uint32_t
NextPartId(const Document& doc)
{
	uint32_t id = 0;
	for (const PartRecord& p : doc.parts.parts)
		if (p.data.size() >= 20)
			id = std::max(id, uint32_t(p.data[16]) << 24 | uint32_t(p.data[17]) << 16
				| uint32_t(p.data[18]) << 8 | p.data[19]);
	return id + 1;
}


// 'PART', the kind, TPart's header: no message, 0, the id, none, 1, the
// selection
void
WritePartHeader(DocWriter& w, uint32_t kind, uint32_t id, uint32_t selection)
{
	w.Sig(Sig("PART"));
	w.Sig(kind);
	w.UInt32(0);
	w.UInt32(0);
	w.UInt32(id);
	w.UInt32(kNone);
	w.UInt32(1);
	w.UInt32(selection);
}


// a selection of the part (TSelection::Write: the kind, the part, its id,
// then the kind's own): appended to the document's, as AddSheet does
uint32_t
AddSelection(Document& doc, uint32_t kind, uint32_t part)
{
	uint32_t id = uint32_t(doc.selections.selections.size());
	DocWriter body;
	body.Sig(kind);
	body.UInt32(part);
	body.UInt32(id);
	if (kind == Sig("WORD")) {
		// TWPSelection: no ranges, two flags
		body.UInt32(0);
		body.Bool(false);
		body.Bool(false);
	} else
		body.Bool(false);	// TIMSelection: no rectangle
	DocWriter record;
	record.UInt32(uint32_t(body.Data().size()));
	record.Bytes(body.Data());
	doc.addedSelections.push_back(record.Data());
	Selection s;
	s.id = id;
	s.kind = kind;
	s.part = part;
	s.value = id;
	doc.selections.selections.push_back(s);
	return id;
}


// a tail with no references and these portals
std::vector<uint8_t>
Tail(const std::vector<Portal>& portals, uint32_t kind)
{
	PartTail tail;
	DocWriter refs;
	refs.Sig(Sig("REFT"));
	refs.UInt32(1);
	refs.UInt32(0);
	refs.Sig(Sig("RefT"));
	tail.refsRaw = refs.Data();
	tail.portals = portals;
	return WritePartTail(tail, kind);
}


// a frame of `part` in `container` (TFrame's fields; `raw` the class's)
Frame
MakeFrame(uint32_t part, uint32_t container, float x, float y, float width, float height,
	uint32_t style, uint32_t flags, std::vector<uint8_t> raw)
{
	Frame f;
	f.origin[0] = x;
	f.origin[1] = y;
	f.extent[0] = width;
	f.extent[1] = height;
	f.style = style;
	f.shape[0] = 0;
	f.shape[1] = kNone;
	f.shape[2] = kNone;
	f.part = part;
	f.container = container;
	f.values[3] = flags;
	f.raw = std::move(raw);
	return f;
}


// the factory for a kind the document hasn't had
void
AddFactory(Document& doc, uint32_t kind, const char* name)
{
	for (const PartFactoryEntry& f : doc.factories)
		if (f.kind == kind)
			return;
	doc.factories.push_back({kind, 1, name});
	doc.factoriesChanged = true;
}


// the part's record added, its tail read
bool
AddRecord(Document& doc, uint32_t kind, std::vector<uint8_t> record, std::string& error)
{
	size_t part = doc.parts.parts.size();
	doc.parts.parts.push_back({kind, 0, std::move(record)});
	doc.tails.resize(doc.parts.parts.size());
	doc.tailRead.resize(doc.parts.parts.size(), false);
	DocArchive versions = Versions(doc);
	doc.tailRead[part] = ReadPartTail(doc.parts.parts[part].data, kind, versions,
		doc.tails[part]);
	if (!doc.tailRead[part]) {
		error = "a new part's tail doesn't read";
		return false;
	}
	doc.dirtyTails.insert(part);
	return true;
}


// the style the container's frame has (the cells' and pictures' frames
// take it, as frames placed in that text)
uint32_t
FrameStyle(const Document& doc, size_t container)
{
	if (container < doc.tails.size())
		for (const Portal& p : doc.tails[container].portals)
			for (const Frame& f : p.frames)
				return f.style;
	return 0;
}


// TInputArchive::WriteBitmap: 'BMAP', the size of the bits, an unused
// bool, the color space, bytes per row, the bounds, compressed (zlib, its
// size, the data), 'BmaP'; an empty one is 'BMAP', 0, 'BmaP'
void
WriteBitmap(DocWriter& w, const ArchiveBitmap& bitmap)
{
	w.Sig(Sig("BMAP"));
	w.UInt32(uint32_t(bitmap.bits.size()));
	if (!bitmap.bits.empty()) {
		w.Bool(false);
		w.UInt32(bitmap.colorSpace);
		w.UInt32(bitmap.bytesPerRow);
		w.Rect(bitmap.bounds);
		uLongf size = compressBound(uLong(bitmap.bits.size()));
		std::vector<uint8_t> packed(size);
		if (compress2(packed.data(), &size, bitmap.bits.data(), uLong(bitmap.bits.size()), 6)
				== Z_OK) {
			packed.resize(size);
			w.Bool(true);
			w.UInt32(uint32_t(packed.size()));
			w.Bytes(packed);
		} else {
			w.Bool(false);
			w.Bytes(bitmap.bits);
		}
	}
	w.Sig(Sig("BmaP"));
}


uint32_t
DefaultStyle(const Document& doc)
{
	for (size_t i = 0; i < doc.styles.styles.size(); i++)
		if (doc.styles.styles[i].name == "Default" && doc.styles.styles[i].value10 == Sig("Basi"))
			return uint32_t(i);
	return kNone;
}

} // namespace


int
AddTablePart(Document& doc, size_t container, const NewTable& table, size_t& textPart,
	std::string& error)
{
	size_t rows = table.rows.size(), columns = table.columns.size();
	if (rows == 0 || columns == 0 || table.cells.size() != rows
			|| doc.WordPart(container) == nullptr) {
		error = "an empty table";
		return -1;
	}
	uint32_t base = DefaultStyle(doc);
	if (base == kNone) {
		error = "no Default style";
		return -1;
	}
	uint32_t id = NextPartId(doc);
	uint32_t tablePart = uint32_t(doc.parts.parts.size()), text = tablePart + 1;
	uint32_t frameStyle = FrameStyle(doc, container);
	float width = 0, height = 0;
	for (float w : table.columns)
		width += w;
	for (float h : table.rows)
		height += h;

	// the text part first (its paragraphs give the cells' ranges)
	WordStyles wordStyles = AddWordStyles(doc.styles, base);
	doc.stylesChanged = true;
	uint32_t textSelection = AddSelection(doc, Sig("WORD"), text);

	// TTablePart: TPart's header (no selection), TTable::Write (its size,
	// the cell inset, the rows: TRow::Write's two heights, its cells:
	// TCell::Write's location, origin, size, spans, frame), the text
	// part and its portal
	DocWriter body;
	WritePartHeader(body, Sig("TBLE"), id, kNone);
	float size[2] = {width, height};
	body.Point(size);
	body.Float(table.inset);
	body.UInt32(uint32_t(rows));
	float y = 0;
	uint32_t frameIndex = 0;
	for (size_t r = 0; r < rows; r++) {
		body.Float(table.rows[r]);
		body.Float(table.rows[r]);
		body.UInt32(uint32_t(columns));
		float x = 0;
		for (size_t c = 0; c < columns; c++) {
			body.UInt32(uint32_t(r));
			body.UInt32(uint32_t(c));
			float origin[2] = {x, y}, cell[2] = {table.columns[c], table.rows[r]};
			body.Point(origin);
			body.Point(cell);
			body.UInt32(1);		// row span
			body.UInt32(1);		// column span
			body.UInt32(text);
			body.UInt32(0);
			body.UInt32(frameIndex++);
			x += table.columns[c];
		}
		y += table.rows[r];
	}
	body.UInt32(text);
	body.UInt32(0);
	// its frame in the container (TTableFrame adds nothing)
	Portal shown;
	shown.part = uint32_t(container);
	shown.values[2] = 1;
	shown.frames.push_back(MakeFrame(tablePart, uint32_t(container), 0, 0, width, height,
		frameStyle, 64, {}));
	std::vector<uint8_t> record = body.Data();
	std::vector<uint8_t> tail = Tail({shown}, Sig("TBLE"));
	record.insert(record.end(), tail.begin(), tail.end());
	if (!AddRecord(doc, Sig("TBLE"), std::move(record), error))
		return -1;
	DocArchive versions = Versions(doc);
	try {
		DocArchive a(doc.parts.parts[tablePart].data);
		a.CopyVersions(versions);
		TablePart parsed;
		ReadTablePart(a, parsed);
		doc.tableParts.emplace_back(tablePart, std::move(parsed));
	} catch (const ArchiveError& x) {
		error = std::string("new table: ") + x.what();
		return -1;
	}

	// the text part: a paragraph per cell, then the cells' paragraphs
	doc.parts.parts.push_back({Sig("WORD"), 0, WordPartRecord(id + 1, textSelection,
		wordStyles.paragraph, wordStyles.run, false, 0)});
	doc.tails.resize(doc.parts.parts.size());
	doc.tailRead.resize(doc.parts.parts.size(), false);
	try {
		DocArchive a(doc.parts.parts[text].data);
		a.CopyVersions(versions);
		WPPart wp;
		ReadWPPart(a, wp);
		doc.wordParts.emplace_back(text, std::move(wp));
	} catch (const ArchiveError& x) {
		error = std::string("new table text: ") + x.what();
		return -1;
	}
	doc.tailRead[text] = ReadPartTail(doc.parts.parts[text].data, Sig("WORD"), versions,
		doc.tails[text]);
	WPPart* wp = doc.EditableWordPart(text);
	if (!doc.tailRead[text] || wp == nullptr) {
		error = "a new table's text doesn't read";
		return -1;
	}
	std::string all;
	std::vector<size_t> paragraphsOf;	// each cell's paragraph count
	for (size_t r = 0; r < rows; r++)
		for (size_t c = 0; c < columns; c++) {
			const std::string& cellText = c < table.cells[r].size() ? table.cells[r][c] : "";
			if (!all.empty() || r + c > 0)
				all += '\n';
			all += cellText;
			paragraphsOf.push_back(size_t(std::count(cellText.begin(), cellText.end(), '\n')) + 1);
		}
	InsertMultiline(*wp, {0, 0}, all);
	// the cells' frames: inset 2 across and 1 down, their ranges
	Portal cells;
	cells.part = tablePart;
	cells.values[1] = uint32_t(rows * columns);
	cells.values[2] = uint32_t(rows * columns);
	DocWriter portalRaw;
	portalRaw.Float(0);		// TWPPortal
	cells.raw = portalRaw.Data();
	size_t paragraph = 0;
	uint32_t at = 0;
	y = 0;
	for (size_t r = 0; r < rows; r++) {
		float x = 0;
		for (size_t c = 0; c < columns; c++) {
			uint32_t length = 0;
			size_t count = paragraphsOf[r * columns + c];
			for (size_t k = 0; k < count && paragraph < wp->paragraphs.size(); k++, paragraph++)
				length += uint32_t(wp->paragraphs[paragraph].text.size());
			// TWPFrame: its text range, then 0, 0, 15 and 1 as in the
			// documents' cells
			DocWriter raw;
			raw.UInt32(at);
			raw.UInt32(length);
			raw.Float(0);
			raw.Float(0);
			raw.Float(15);
			raw.Float(1);
			Frame f = MakeFrame(text, tablePart, x + 2, y + 1,
				std::max(1.0f, table.columns[c] - 4), std::max(1.0f, table.rows[r] - 2),
				frameStyle, 72, raw.Data());
			f.values[1] = uint32_t(r * columns + c);
			f.textStart = at;
			f.textLength = length;
			f.textAt = 1;
			cells.frames.push_back(f);
			at += length;
			x += table.columns[c];
		}
		y += table.rows[r];
	}
	doc.tails[text].portals = {cells};
	doc.dirtyTails.insert(text);
	doc.layoutRanges.insert(text);
	AddFactory(doc, Sig("TBLE"), "Table Frame");
	textPart = text;
	return int(tablePart);
}


const Frame*
FrameAt(const Document& doc, const uint32_t pointer[3])
{
	if (pointer[0] >= doc.tails.size() || !doc.tailRead[pointer[0]])
		return nullptr;
	for (const Portal& p : doc.tails[pointer[0]].portals)
		if (p.values[0] == pointer[1])
			for (const Frame& f : p.frames)
				if (f.values[1] == pointer[2])
					return &f;
	return nullptr;
}


int
CellParagraph(const Document& doc, size_t table, size_t row, size_t column)
{
	const TablePart* t = doc.Table(table);
	if (t == nullptr || row >= t->rows.size() || column >= t->rows[row].cells.size())
		return -1;
	const TableCell& cell = t->rows[row].cells[column];
	const Frame* f = FrameAt(doc, cell.frame);
	const WPPart* wp = doc.WordPart(t->textPart);
	if (f == nullptr || wp == nullptr)
		return -1;
	uint32_t at = 0;
	for (size_t k = 0; k < wp->paragraphs.size(); k++) {
		if (at == f->textStart)
			return int(k);
		at += uint32_t(wp->paragraphs[k].text.size());
	}
	return -1;
}


void
SetCellRanges(Document& doc, size_t table, const std::vector<size_t>& paragraphs)
{
	const TablePart* t = doc.Table(table);
	const WPPart* wp = t != nullptr ? doc.WordPart(t->textPart) : nullptr;
	if (wp == nullptr || t->textPart >= doc.tails.size())
		return;
	size_t paragraph = 0, cell = 0;
	uint32_t at = 0;
	for (const TableRow& row : t->rows)
		for (const TableCell& c : row.cells) {
			uint32_t length = 0;
			size_t count = cell < paragraphs.size() ? paragraphs[cell] : 1;
			for (size_t k = 0; k < count && paragraph < wp->paragraphs.size(); k++, paragraph++)
				length += uint32_t(wp->paragraphs[paragraph].text.size());
			for (Portal& p : doc.tails[t->textPart].portals)
				if (p.values[0] == c.frame[1])
					for (Frame& f : p.frames)
						if (f.values[1] == c.frame[2]) {
							f.textStart = at;
							f.textLength = length;
						}
			at += length;
			cell++;
		}
	doc.dirtyTails.insert(t->textPart);
}


int
AddImagePart(Document& doc, size_t container, const ArchiveBitmap& bitmap, std::string& error)
{
	if (bitmap.Empty() || doc.WordPart(container) == nullptr) {
		error = "no picture";
		return -1;
	}
	uint32_t id = NextPartId(doc);
	uint32_t part = uint32_t(doc.parts.parts.size());
	uint32_t selection = AddSelection(doc, Sig("BITS"), part);
	float width = float(bitmap.Width()), height = float(bitmap.Height());
	// TPartIM::Write: 'imp+', the size, the bitmap, the floating one (none),
	// the selection rectangle (the whole), the colors, 'imp-'
	DocWriter body;
	WritePartHeader(body, Sig("BITS"), id, selection);
	body.Sig(Sig("imp+"));
	float size[2] = {width, height};
	body.Point(size);
	WriteBitmap(body, bitmap);
	WriteBitmap(body, ArchiveBitmap());
	float rect[4] = {0, 0, width - 1, height - 1};
	body.Rect(rect);
	for (uint8_t v : {0, 0, 0, 255, 255, 255, 255, 255})
		body.UInt8(v);
	body.Sig(Sig("imp-"));
	// its frame in the container (TFrameIM: the bitmap's rectangle shown)
	Portal shown;
	shown.part = uint32_t(container);
	shown.values[2] = 1;
	DocWriter raw;
	raw.Rect(rect);
	Frame f = MakeFrame(part, uint32_t(container), 0, 0, width, height,
		FrameStyle(doc, container), 0, raw.Data());
	f.shape[1] = kNone;
	for (int i = 0; i < 4; i++)
		f.sheetRect[i] = rect[i];
	shown.frames.push_back(f);
	std::vector<uint8_t> record = body.Data();
	std::vector<uint8_t> tail = Tail({shown}, Sig("BITS"));
	record.insert(record.end(), tail.begin(), tail.end());
	if (!AddRecord(doc, Sig("BITS"), std::move(record), error))
		return -1;
	DocArchive versions = Versions(doc);
	try {
		DocArchive a(doc.parts.parts[part].data);
		a.CopyVersions(versions);
		IMPart parsed;
		ReadIMPart(a, parsed);
		doc.imageParts.emplace_back(part, std::move(parsed));
	} catch (const ArchiveError& x) {
		error = std::string("new picture: ") + x.what();
		return -1;
	}
	AddFactory(doc, Sig("BITS"), "Image Processing");
	return int(part);
}


TextPosition
InsertInlineFrame(Document& doc, size_t container, TextPosition at, size_t part)
{
	WPPart* wp = doc.EditableWordPart(container);
	if (wp == nullptr)
		return at;
	// TMiscFrameInfo: the frame pointer (the part, its portal, its frame)
	WPMiscInfo frame;
	frame.kind = Sig("FRAM");
	frame.frame[0] = uint32_t(part);
	frame.frame[1] = 0;
	frame.frame[2] = 0;
	DocWriter raw;
	for (uint32_t v : frame.frame)
		raw.UInt32(v);
	frame.raw = raw.Data();
	return InsertField(*wp, at, frame);
}

} // namespace gobe
