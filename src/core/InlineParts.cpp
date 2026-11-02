// Table and image parts made in a document (see InlineParts.h), written
// as TTablePart::Write, TPartIM::Write and the parts' tails (TPortal,
// TFrame and its subclasses) lay them out.
#include "InlineParts.h"

#include <algorithm>
#include <cmath>

#include <zlib.h>

#include "BlankParts.h"
#include "DocWriter.h"
#include "Document.h"
#include "Editing.h"
#include "GRPart.h"
#include "SSPart.h"
#include "PartTail.h"
#include "Styles.h"
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


// a style based on `base` with these properties of its own (the
// properties found in the style manager's list, else added)
uint32_t
DerivedStyle(StyleMgr& styles, uint32_t base, const std::vector<Property>& properties)
{
	Style style;
	if (base < styles.styles.size()) {
		const Style& b = styles.styles[base];
		style.id = b.id;
		style.value4 = b.value4;
		style.value10 = b.value10;
	}
	style.links[0] = base;
	style.links[1] = kNone;
	style.links[2] = kNone;
	for (const Property& p : properties) {
		size_t i = 0;
		while (i < styles.properties.size() && !(styles.properties[i].type == p.type
				&& styles.properties[i].data == p.data))
			i++;
		if (i == styles.properties.size())
			styles.properties.push_back(p);
		style.props.push_back(uint32_t(i));
	}
	styles.styles.push_back(style);
	return uint32_t(styles.styles.size() - 1);
}


// a style property: its type and the bytes of a float
Property
FloatProperty(uint32_t type, float value)
{
	DocWriter w;
	w.Float(value);
	return Property{type, w.Data()};
}

} // namespace


uint32_t
NewFrameStyle(Document& doc)
{
	// TFrame::TFrame: TStyleManager::NewStyle('Basi', no pen, no fill, pen
	// size 0, based on "Default"), an identical unnamed one if there is
	std::vector<Property> props = {FloatProperty(Sig("FTra"), 0), FloatProperty(Sig("PENz"), 0),
		FloatProperty(Sig("PTra"), 0)};
	uint32_t base = 0;
	for (size_t i = 0; i < doc.styles.styles.size(); i++)
		if (doc.styles.styles[i].name == "Default" && doc.styles.styles[i].value10 == Sig("Basi"))
			base = uint32_t(i);
	for (size_t i = 0; i < doc.styles.styles.size(); i++) {
		const Style& s = doc.styles.styles[i];
		if (!s.name.empty() || s.value10 != Sig("Basi") || s.links[0] != base
				|| s.links[1] != kNone || s.props.size() != props.size())
			continue;
		bool same = true;
		for (size_t k = 0; k < props.size() && same; k++) {
			const Property& p = doc.styles.properties[s.props[k]];
			same = std::any_of(props.begin(), props.end(), [&](const Property& q) {
				return q.type == p.type && q.data == p.data;
			});
		}
		if (same)
			return uint32_t(i);
	}
	doc.stylesChanged = true;
	return DerivedStyle(doc.styles, base, props);
}


namespace {

uint32_t
DefaultStyle(const Document& doc)
{
	for (size_t i = 0; i < doc.styles.styles.size(); i++)
		if (doc.styles.styles[i].name == "Default" && doc.styles.styles[i].value10 == Sig("Basi"))
			return uint32_t(i);
	return kNone;
}

} // namespace


NewTable
MakeNewTable(size_t rows, size_t columns)
{
	NewTable table;
	if (rows == 0 || columns == 0)
		return table;
	table.columns.assign(columns, 400.0f / float(columns));
	table.rows.assign(rows, 17.0f);
	table.inset = 1;
	table.border = 1;
	table.cells.assign(rows, std::vector<std::string>(columns));
	table.cellBreaks = true;
	table.newFrames = true;
	return table;
}


int
AddTablePart(Document& doc, size_t container, const NewTable& table, size_t& textPart,
	std::string& error)
{
	size_t rows = table.rows.size(), columns = table.columns.size();
	if (rows == 0 || columns == 0 || table.cells.size() != rows
			|| container >= doc.parts.parts.size()) {
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
	// its frame in the container (TTableFrame adds nothing), its style the
	// frames' with the border as its pen (TTableImport::DoImport: none
	// transparent, else that size, black)
	Color black;
	Property pen[3] = {Property{Sig("PCol"), {}}, Property{Sig("PENz"), {}},
		Property{Sig("PTra"), {}}};
	{
		Ink ink;
		ink.set = true;
		ink.color = black;
		ink.low = {255, 255, 255, 255};
		pen[0].data = InkBytes(ink);
		DocWriter size, opacity;
		size.Float(table.border > 0 ? table.border : 1);
		opacity.Float(table.border > 0 ? 1 : 0);
		pen[1].data = size.Data();
		pen[2].data = opacity.Data();
	}
	// a table made in Gobe: TTableFrame, the frames' style with an opaque
	// pen (TStyleID::SetPenTransparent 1.0)
	uint32_t tableStyle = table.newFrames
		? StyleWithProperty(doc.styles, NewFrameStyle(doc), FloatProperty(Sig("PTra"), 1))
		: DerivedStyle(doc.styles, frameStyle, {pen[0], pen[1], pen[2]});
	Portal shown;
	shown.part = uint32_t(container);
	shown.values[2] = 1;
	shown.frames.push_back(MakeFrame(tablePart, uint32_t(container), 0, 0, width, height,
		tableStyle, 64, {}));
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
	{
		// (TTablePart::TTablePart: the text part's +0x98, after 'wp++', is 2)
		std::vector<uint8_t>& data = doc.parts.parts.back().data;
		DocWriter sig, two;
		sig.Sig(Sig("wp++"));
		two.UInt16(2);
		auto at = std::search(data.begin(), data.end(), sig.Data().begin(), sig.Data().end());
		if (at != data.end() && at + 4 + 2 <= data.end())
			std::copy(two.Data().begin(), two.Data().end(), at + 4);
	}
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
			if (!table.cellBreaks && (!all.empty() || r + c > 0))
				all += '\n';
			all += cellText;
			paragraphsOf.push_back(size_t(std::count(cellText.begin(), cellText.end(), '\n')) + 1);
		}
	if (table.cellBreaks) {
		// each cell's last paragraph ended by its cell break
		TextPosition at = {0, 0};
		size_t cell = 0;
		for (size_t r = 0; r < rows; r++)
			for (size_t c = 0; c < columns; c++, cell++) {
				const std::string& cellText = c < table.cells[r].size() ? table.cells[r][c] : "";
				at = InsertMultiline(*wp, at, cellText);
				at = InsertSpecialChar(*wp, at, 0x05);
			}
	} else
		InsertMultiline(*wp, {0, 0}, all);
	// (TTable::SetUp: new frames, TFrame::TFrame's style)
	uint32_t cellStyle = table.newFrames ? NewFrameStyle(doc) : frameStyle;
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
			float across = table.inset + 1, down = table.inset;
			Frame f = MakeFrame(text, tablePart, x + across, y + down,
				std::max(1.0f, table.columns[c] - 2 * across),
				std::max(1.0f, table.rows[r] - 2 * down), cellStyle, 72, raw.Data());
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
	if (bitmap.Empty() || container >= doc.parts.parts.size()) {
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

// #pragma mark - frames in drawings


namespace {

bool
IsDrawing(const Document& doc, size_t part)
{
	return part < doc.parts.parts.size() && (doc.parts.parts[part].kind == Sig("GRFX")
		|| doc.parts.parts[part].kind == Sig("PRES")) && doc.GraphicsPart(part) != nullptr;
}


gobe::Frame*
MutableFrame(Document& doc, const uint32_t pointer[3])
{
	return const_cast<gobe::Frame*>(FrameAt(doc, pointer));
}


// the next shape id in a drawing (TShape +0x20): after its shapes' and
// the frames it places
uint32_t
NextShapeId(const Document& doc, size_t drawing)
{
	uint32_t id = 0;
	const GRPart* gr = doc.GraphicsPart(drawing);
	for (const GRLayer& l : gr->layers)
		for (const Shape& s : l.shapes) {
			if (s.kind != Sig("FRAM"))
				id = std::max(id, s.values[2] + 1);
			else if (const gobe::Frame* f = FrameAt(doc, s.frame))
				id = std::max(id, f->shape[2] + 1);
		}
	return id;
}


// a WORD part with one empty paragraph, its styles and selection (as
// AddSheet makes a text sheet's), read in; its index or -1
int
NewWordPart(Document& doc, std::string& error)
{
	uint32_t base = DefaultStyle(doc);
	if (base == kNone) {
		error = "no Default style";
		return -1;
	}
	uint32_t part = uint32_t(doc.parts.parts.size());
	WordStyles styles = AddWordStyles(doc.styles, base);
	doc.stylesChanged = true;
	uint32_t selection = AddSelection(doc, Sig("WORD"), part);
	doc.parts.parts.push_back({Sig("WORD"), 0, WordPartRecord(NextPartId(doc), selection,
		styles.paragraph, styles.run, false, 0)});
	doc.tails.resize(doc.parts.parts.size());
	doc.tailRead.resize(doc.parts.parts.size(), false);
	DocArchive versions = Versions(doc);
	try {
		DocArchive a(doc.parts.parts[part].data);
		a.CopyVersions(versions);
		WPPart wp;
		ReadWPPart(a, wp);
		doc.wordParts.emplace_back(part, std::move(wp));
	} catch (const ArchiveError& x) {
		error = std::string("new text: ") + x.what();
		return -1;
	}
	doc.tailRead[part] = ReadPartTail(doc.parts.parts[part].data, Sig("WORD"), versions,
		doc.tails[part]);
	if (!doc.tailRead[part]) {
		error = "a new text's tail doesn't read";
		return -1;
	}
	return int(part);
}

} // namespace


int
ClonePart(Document& doc, size_t part, std::string& error)
{
	if (part >= doc.parts.parts.size() || part >= doc.tails.size() || !doc.tailRead[part]) {
		error = "no such part";
		return -1;
	}
	PartRecord source = doc.parts.parts[part];
	std::vector<uint8_t> record = source.data;
	if (record.size() < 32) {
		error = "a part too short to copy";
		return -1;
	}
	// TPart::TPart: an id of its own (and, for text and pictures, a
	// selection of its own)
	uint32_t id = NextPartId(doc);
	for (int i = 0; i < 4; i++)
		record[16 + i] = uint8_t(id >> (24 - 8 * i));
	size_t clone = doc.parts.parts.size();
	if (source.kind == Sig("WORD") || source.kind == Sig("BITS")) {
		uint32_t selection = AddSelection(doc, source.kind, uint32_t(clone));
		for (int i = 0; i < 4; i++)
			record[28 + i] = uint8_t(selection >> (24 - 8 * i));
	}
	if (!AddRecord(doc, source.kind, std::move(record), error))
		return -1;
	// its contents as the original's were read (the record is the same)
	if (const WPPart* wp = doc.WordPart(part))
		doc.wordParts.emplace_back(clone, WPPart(*wp));
	else if (const GRPart* gr = doc.GraphicsPart(part))
		doc.graphicsParts.emplace_back(clone, GRPart(*gr));
	else if (const IMPart* im = doc.ImagePart(part))
		doc.imageParts.emplace_back(clone, IMPart(*im));
	else if (const SSPart* ss = doc.SpreadsheetPart(part))
		doc.spreadsheetParts.emplace_back(clone, SSPart(*ss));
	else if (const ChartPart* chart = doc.Chart(part))
		doc.chartParts.emplace_back(clone, gobe::ChartPart(*chart));
	else if (const TablePart* table = doc.Table(part))
		doc.tableParts.emplace_back(clone, gobe::TablePart(*table));
	doc.dirtyParts.insert(clone);
	return int(clone);
}


bool
CopyLayerShapes(Document& doc, size_t drawing, size_t from, size_t to, std::string& error)
{
	const GRPart* gr = doc.GraphicsPart(drawing);
	if (gr == nullptr || from >= gr->layers.size() || to >= gr->layers.size()) {
		error = "no such layer";
		return false;
	}
	std::vector<Shape> shapes = gr->layers[from].shapes;
	for (Shape& shape : shapes) {
		if (shape.kind == Sig("FRAM")) {
			// TPartGR::InsertChildFrame of a copy of its part
			int clone = ClonePart(doc, shape.frame[0], error);
			if (clone < 0)
				return false;
			shape.frame[0] = uint32_t(clone);
			if (gobe::Frame* frame = MutableFrame(doc, shape.frame)) {
				frame->shape[1] = 0;
				frame->shape[2] = NextShapeId(doc, drawing);
			}
		} else
			shape.values[2] = NextShapeId(doc, drawing);
		GRPart* edit = doc.EditableGraphicsPart(drawing);
		edit->layers[to].shapes.push_back(shape);
	}
	doc.ComputeHiddenFrames();
	return true;
}


bool
AddFrameShape(Document& doc, size_t drawing, size_t layer, const uint32_t pointer[3])
{
	gobe::Frame* frame = MutableFrame(doc, pointer);
	if (!IsDrawing(doc, drawing) || frame == nullptr)
		return false;
	// a frame in a drawing is one of its shapes: its id among them
	frame->shape[1] = 0;
	frame->shape[2] = NextShapeId(doc, drawing);
	doc.dirtyTails.insert(pointer[0]);
	GRPart* gr = doc.EditableGraphicsPart(drawing);
	if (gr->layers.empty())
		gr->layers.push_back({"Layer 1", {}});
	if (layer >= gr->layers.size())
		layer = gr->layers.size() - 1;
	// TPartGR::InsertChildFrame: a FRAM entry pointing at it
	Shape s;
	s.kind = Sig("FRAM");
	for (int i = 0; i < 3; i++)
		s.frame[i] = pointer[i];
	gr->layers[layer].shapes.push_back(s);
	return true;
}


int
AddDrawingFrame(Document& doc, size_t drawing, size_t layer, uint32_t kind, const float rect[4],
	std::string& error, const NewTable* given)
{
	if (!IsDrawing(doc, drawing)) {
		error = "not a drawing";
		return -1;
	}
	float x = std::min(rect[0], rect[2]), y = std::min(rect[1], rect[3]);
	float width = std::fabs(rect[2] - rect[0]), height = std::fabs(rect[3] - rect[1]);
	if (given == nullptr && (width < 4 || height < 4)) {
		error = "too small";
		return -1;
	}
	// (TFrame::TFrame: a new frame's style, no fill and no pen; a table's
	// its TTableFrame's, as AddTablePart makes it)
	uint32_t frameStyle = NewFrameStyle(doc);
	doc.stylesChanged = true;
	int made = -1;
	if (kind == Sig("TBLE")) {
		// the table given, at its size; or the part tool's one cell, the
		// frame's size
		NewTable table = MakeNewTable(1, 1);
		if (given != nullptr) {
			table = *given;
			width = height = 0;
			for (float w : table.columns)
				width += w;
			for (float h : table.rows)
				height += h;
		} else {
			table.columns = {width};
			table.rows = {height};
		}
		size_t text = 0;
		made = AddTablePart(doc, drawing, table, text, error);
	} else if (kind == Sig("BITS")) {
		// an empty (white) picture of the frame's size
		ArchiveBitmap bitmap;
		bitmap.colorSpace = 0x2008;
		uint32_t w = uint32_t(std::max(1.0f, std::round(width)));
		uint32_t h = uint32_t(std::max(1.0f, std::round(height)));
		bitmap.bytesPerRow = w * 4;
		bitmap.bounds[2] = float(w - 1);
		bitmap.bounds[3] = float(h - 1);
		bitmap.bits.assign(size_t(w) * h * 4, 255);
		made = AddImagePart(doc, drawing, bitmap, error);
	} else if (kind == Sig("WORD")) {
		made = NewWordPart(doc, error);
		if (made >= 0) {
			// TWPFrame: the text (none), then 0, 0, 15, 0
			DocWriter raw;
			raw.UInt32(0);
			raw.UInt32(uint32_t(doc.WordPart(size_t(made))->paragraphs.empty() ? 0
				: doc.WordPart(size_t(made))->paragraphs[0].text.size()));
			for (float v : {0.0f, 0.0f, 15.0f, 0.0f})
				raw.Float(v);
			Portal shown;
			shown.part = uint32_t(drawing);
			shown.values[2] = 1;
			DocWriter portalRaw;
			portalRaw.Float(0);
			shown.raw = portalRaw.Data();
			gobe::Frame f = MakeFrame(uint32_t(made), uint32_t(drawing), x, y, width, height,
				frameStyle, 0, raw.Data());
			f.textAt = 1;
			f.textLength = uint32_t(doc.WordPart(size_t(made))->paragraphs.empty() ? 0
				: doc.WordPart(size_t(made))->paragraphs[0].text.size());
			shown.frames.push_back(f);
			doc.tails[size_t(made)].portals = {shown};
			doc.dirtyTails.insert(size_t(made));
			doc.dirtyParts.insert(size_t(made));
		}
	} else if (kind == Sig("SPRD") || kind == Sig("GRFX")) {
		uint32_t base = DefaultStyle(doc);
		if (base == kNone) {
			error = "no Default style";
			return -1;
		}
		uint32_t part = uint32_t(doc.parts.parts.size());
		uint32_t id = NextPartId(doc);
		DocArchive versions = Versions(doc);
		Portal shown;
		shown.part = uint32_t(drawing);
		shown.values[2] = 1;
		DocWriter raw;
		if (kind == Sig("SPRD")) {
			// AddSheet's: the spreadsheet styles, its selection (A1)
			SheetStyles styles = AddSheetStyles(doc.styles, base);
			doc.stylesChanged = true;
			uint32_t selection = uint32_t(doc.selections.selections.size());
			DocWriter body;
			body.Sig(Sig("SPRD"));
			body.UInt32(part);
			body.UInt32(selection);
			body.Sig(Sig("CELL"));
			body.UInt16(0);
			body.UInt16(0);
			body.UInt32(1);
			for (int i = 0; i < 4; i++)
				body.UInt16(0);
			body.Bool(true);
			DocWriter record;
			record.UInt32(uint32_t(body.Data().size()));
			record.Bytes(body.Data());
			doc.addedSelections.push_back(record.Data());
			Selection s;
			s.id = selection;
			s.kind = Sig("SPRD");
			s.part = part;
			s.value = selection;
			s.ranges.push_back({0, 0, 0, 0});
			s.hasCell = true;
			doc.selections.selections.push_back(s);
			doc.parts.parts.push_back({Sig("SPRD"), 0, SheetPartRecord(id, selection, styles,
				false, 0)});
			// TSSFrame: the cells shown (from A1, as many as fit), the
			// rectangle
			raw.UInt16(0);
			raw.UInt16(0);
			raw.UInt16(uint16_t(std::ceil(height / 16)));
			raw.UInt16(uint16_t(std::ceil(width / 72)));
		} else {
			uint32_t selection = uint32_t(doc.selections.selections.size());
			DocWriter body;
			body.Sig(Sig("GRFX"));
			body.UInt32(part);
			body.UInt32(selection);
			body.UInt32(0);
			DocWriter record;
			record.UInt32(uint32_t(body.Data().size()));
			record.Bytes(body.Data());
			doc.addedSelections.push_back(record.Data());
			Selection s;
			s.id = selection;
			s.kind = Sig("GRFX");
			s.part = part;
			s.value = selection;
			doc.selections.selections.push_back(s);
			doc.parts.parts.push_back({Sig("GRFX"), 0, SheetGraphicsRecord(id, selection, false, 0)});
			DocWriter portalRaw;
			portalRaw.UInt32(0);		// TGRPortal: view 0
			shown.raw = portalRaw.Data();
		}
		float shownRect[4] = {0, 0, width, height};
		raw.Rect(shownRect);
		doc.tails.resize(doc.parts.parts.size());
		doc.tailRead.resize(doc.parts.parts.size(), false);
		try {
			DocArchive a(doc.parts.parts[part].data);
			a.CopyVersions(versions);
			if (kind == Sig("SPRD")) {
				SSPart ss;
				ReadSSPart(a, ss);
				doc.spreadsheetParts.emplace_back(part, std::move(ss));
			} else {
				GRPart gr;
				ReadGRPart(a, gr);
				doc.graphicsParts.emplace_back(part, std::move(gr));
			}
		} catch (const ArchiveError& x) {
			error = std::string("new part: ") + x.what();
			return -1;
		}
		doc.tailRead[part] = ReadPartTail(doc.parts.parts[part].data, kind, versions,
			doc.tails[part]);
		if (!doc.tailRead[part]) {
			error = "a new part's tail doesn't read";
			return -1;
		}
		gobe::Frame f = MakeFrame(part, uint32_t(drawing), x, y, width, height, frameStyle, 0,
			raw.Data());
		for (int i = 0; i < 4; i++)
			f.sheetRect[i] = shownRect[i];
		if (kind == Sig("SPRD")) {
			f.cells[2] = uint16_t(std::ceil(height / 16));
			f.cells[3] = uint16_t(std::ceil(width / 72));
		}
		shown.frames.push_back(f);
		doc.tails[part].portals = {shown};
		doc.dirtyTails.insert(part);
		AddFactory(doc, kind, kind == Sig("SPRD") ? "Spreadsheet" : "Graphics");
		made = int(part);
	} else {
		error = "frames of that kind can't be made";
		return -1;
	}
	if (made < 0)
		return -1;
	// at the rectangle in the drawing
	gobe::Frame* frame = &doc.tails[size_t(made)].portals[0].frames[0];
	if (kind == Sig("BITS"))
		frame->style = frameStyle;
	frame->origin[0] = x;
	frame->origin[1] = y;
	frame->extent[0] = width;
	frame->extent[1] = height;
	uint32_t pointer[3] = {uint32_t(made), 0, 0};
	if (!AddFrameShape(doc, drawing, layer, pointer)) {
		error = "its frame";
		return -1;
	}
	AddFactory(doc, Sig("WORD"), "Word Processing");
	return made;
}


bool
AddLinkedFrame(Document& doc, const uint32_t pointer[3], uint32_t made[3], std::string& error)
{
	gobe::Frame* frame = MutableFrame(doc, pointer);
	if (frame == nullptr || doc.WordPart(pointer[0]) == nullptr || !IsDrawing(doc, frame->container)) {
		error = "not a text frame in a drawing";
		return false;
	}
	// TDocWindow::MakeLinkedFrame: the part's next frame in the same portal
	// (the text flows on into it), the frame's size, right of it
	Portal* portal = nullptr;
	for (Portal& p : doc.tails[pointer[0]].portals)
		if (p.values[0] == pointer[1])
			portal = &p;
	uint32_t id = 0;
	for (const gobe::Frame& f : portal->frames)
		id = std::max(id, f.values[1] + 1);
	gobe::Frame linked = *frame;
	linked.origin[0] += frame->extent[0] + 18;
	linked.values[1] = id;
	linked.textStart = frame->textStart + frame->textLength;
	linked.textLength = 0;
	portal->frames.push_back(linked);
	portal->values[1] = uint32_t(portal->frames.size());
	portal->values[2] = uint32_t(portal->frames.size());
	doc.dirtyTails.insert(pointer[0]);
	made[0] = pointer[0];
	made[1] = pointer[1];
	made[2] = id;
	size_t layer = 0;
	if (const GRPart* gr = doc.GraphicsPart(frame->container))
		for (size_t l = 0; l < gr->layers.size(); l++)
			for (const Shape& s : gr->layers[l].shapes)
				if (s.kind == Sig("FRAM") && s.frame[0] == pointer[0])
					layer = l;
	return AddFrameShape(doc, portal->part, layer, made);
}


bool
CloneFrame(Document& doc, const uint32_t pointer[3], uint32_t made[3], std::string& error)
{
	const gobe::Frame* frame = FrameAt(doc, pointer);
	if (frame == nullptr || !IsDrawing(doc, frame->container)) {
		error = "not a frame in a drawing";
		return false;
	}
	// TDocWindow::DoCloneFrame: another view of the part (a portal of its
	// own in the same drawing with a frame like it), down and right
	const Portal* source = nullptr;
	for (const Portal& p : doc.tails[pointer[0]].portals)
		if (p.values[0] == pointer[1])
			source = &p;
	uint32_t portalId = 0;
	for (const Portal& p : doc.tails[pointer[0]].portals)
		portalId = std::max(portalId, p.values[0] + 1);
	Portal clone;
	clone.part = source->part;
	clone.values[0] = portalId;
	clone.values[2] = 1;
	clone.raw = source->raw;
	clone.view = source->view;
	gobe::Frame f = *frame;
	f.origin[0] += 18;
	f.origin[1] += 18;
	f.values[1] = 0;
	clone.frames.push_back(f);
	doc.tails[pointer[0]].portals.push_back(clone);
	doc.dirtyTails.insert(pointer[0]);
	made[0] = pointer[0];
	made[1] = portalId;
	made[2] = 0;
	size_t layer = 0;
	if (const GRPart* gr = doc.GraphicsPart(clone.part))
		for (size_t l = 0; l < gr->layers.size(); l++)
			for (const Shape& s : gr->layers[l].shapes)
				if (s.kind == Sig("FRAM") && s.frame[0] == pointer[0])
					layer = l;
	return AddFrameShape(doc, clone.part, layer, made);
}


uint32_t
FrameCommandFlags(uint32_t flags, uint32_t command)
{
	// TFrameCommand::Perform, doAutoGrow: 'sM34' grows down (8), 'sM35'
	// across (4), each turning the scroll bars off; 'sM36' the scroll bars
	// (0x20), turning the growing off
	if (command == Sig("sM34") || command == Sig("sM35")) {
		uint32_t bit = command == Sig("sM34") ? 8 : 4;
		return (flags & bit) ? flags & ~bit : (flags & ~0x20u) | bit;
	}
	if (command == Sig("sM36"))
		return (flags & ~0xcu) ^ 0x20;
	return flags;
}

bool
MoveFrame(Document& doc, const uint32_t pointer[3], float dx, float dy)
{
	gobe::Frame* f = MutableFrame(doc, pointer);
	if (f == nullptr)
		return false;
	f->origin[0] += dx;
	f->origin[1] += dy;
	doc.dirtyTails.insert(pointer[0]);
	return true;
}


bool
ResizeFrame(Document& doc, const uint32_t pointer[3], const float rect[4])
{
	gobe::Frame* f = MutableFrame(doc, pointer);
	float left = std::min(rect[0], rect[2]), top = std::min(rect[1], rect[3]);
	float width = std::fabs(rect[2] - rect[0]), height = std::fabs(rect[3] - rect[1]);
	if (f == nullptr || width < 4 || height < 4)
		return false;
	f->origin[0] = left;
	f->origin[1] = top;
	f->extent[0] = width;
	f->extent[1] = height;
	uint32_t kind = doc.parts.parts[pointer[0]].kind;
	if (kind == Sig("SPRD")) {
		f->sheetRect[2] = f->sheetRect[0] + width;
		f->sheetRect[3] = f->sheetRect[1] + height;
	} else if (kind == Sig("GRFX") || kind == Sig("PRES") || kind == Sig("chrt")) {
		f->sheetRect[2] = f->sheetRect[0] + width;
		f->sheetRect[3] = f->sheetRect[1] + height;
	} else if (kind == Sig("BITS")) {
		// (no more than the image)
		const IMPart* im = doc.ImagePart(pointer[0]);
		float w = im != nullptr && im->size[0] > 0 ? im->size[0] : width;
		float h = im != nullptr && im->size[1] > 0 ? im->size[1] : height;
		f->sheetRect[2] = f->sheetRect[0] + std::min(width, w - f->sheetRect[0]) - 1;
		f->sheetRect[3] = f->sheetRect[1] + std::min(height, h - f->sheetRect[1]) - 1;
	}
	doc.dirtyTails.insert(pointer[0]);
	return true;
}

} // namespace gobe
