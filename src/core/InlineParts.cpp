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
	uint32_t tableStyle = DerivedStyle(doc.styles, frameStyle, {pen[0], pen[1], pen[2]});
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
	std::string& error)
{
	if (!IsDrawing(doc, drawing)) {
		error = "not a drawing";
		return -1;
	}
	float x = std::min(rect[0], rect[2]), y = std::min(rect[1], rect[3]);
	float width = std::fabs(rect[2] - rect[0]), height = std::fabs(rect[3] - rect[1]);
	if (width < 4 || height < 4) {
		error = "too small";
		return -1;
	}
	// the drawing's frames' style with an opaque white background (the
	// port's: the text under a frame isn't wrapped around it)
	Ink white;
	white.set = true;
	white.color = {255, 255, 255, 255};
	white.low = {255, 255, 255, 255};
	DocWriter opaque;
	opaque.Float(1);
	uint32_t frameStyle = DerivedStyle(doc.styles, FrameStyle(doc, drawing),
		{Property{Sig("BCol"), InkBytes(white)}, Property{Sig("BTra"), opaque.Data()}});
	doc.stylesChanged = true;
	int made = -1;
	if (kind == Sig("TBLE")) {
		// TInsertTableDialog's default: 2 x 2, the frame shared
		NewTable table;
		table.columns = {width / 2, width / 2};
		table.rows = {height / 2, height / 2};
		table.cells = {{"", ""}, {"", ""}};
		table.border = 1;
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

} // namespace gobe
