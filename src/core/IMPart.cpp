#include "IMPart.h"

#include <zlib.h>

#include <algorithm>

#include "DocWriter.h"
#include "Document.h"

namespace gobe {

void
ReadBitmap(DocArchive& a, ArchiveBitmap& out)
{
	a.CheckSig(Sig("BMAP"));
	uint32_t size = a.UInt32();
	if (size != 0) {
		a.Bool();
		out.colorSpace = a.UInt32();
		out.bytesPerRow = a.UInt32();
		a.Rect(out.bounds);
		bool compressed = a.FileFormatVersion() > 0x15 && a.Bool();
		if (compressed) {
			uint32_t packed = a.UInt32();
			std::vector<uint8_t> data = a.Bytes(packed);
			out.bits.resize(size);
			uLongf length = size;
			if (uncompress(out.bits.data(), &length, data.data(), packed) != Z_OK
				|| length != size)
				throw ArchiveError("bitmap data doesn't decompress", a.Position());
		} else
			out.bits = a.Bytes(size);
	}
	a.CheckSig(Sig("BmaP"));
}


void
ReadIMPart(DocArchive& a, IMPart& out)
{
	a.CheckSig(Sig("PART"));
	a.UInt32();
	ReadPartHeader(a, out.header);

	out.sectionStart = a.Position();
	a.CheckSig(Sig("imp+"));
	a.Point(out.size);
	ReadBitmap(a, out.image);
	ReadBitmap(a, out.floating);
	a.Rect(out.rect);
	for (auto& c : out.colors)
		for (uint8_t& v : c)
			v = a.UInt8();
	a.CheckSig(Sig("imp-"));
	out.sectionEnd = a.Position();
}


std::vector<uint8_t>
WriteIMSection(const IMPart& part)
{
	DocWriter w;
	w.Sig(Sig("imp+"));
	w.Point(part.size);
	WriteBitmap(w, part.image);
	WriteBitmap(w, part.floating);
	w.Rect(part.rect);
	for (const auto& c : part.colors)
		for (uint8_t v : c)
			w.UInt8(v);
	w.Sig(Sig("imp-"));
	return w.Data();
}


bool
SetImageSize(Document& doc, size_t part, float width, float height)
{
	const IMPart* image = doc.ImagePart(part);
	if (image == nullptr || width < 1 || height < 1)
		return false;
	if (image->size[0] == width && image->size[1] == height)
		return false;
	IMPart* edit = doc.EditableImagePart(part);
	ArchiveBitmap old = edit->image;
	int w = int(width), h = int(height);
	// (32 bits a pixel: B_RGB32, B_RGBA32, kept as BeOS x86 kept them, B, G, R, A)
	if (old.colorSpace == 0x0008 || old.colorSpace == 0x2008 || old.bits.empty()) {
		ArchiveBitmap made;
		made.colorSpace = old.bits.empty() ? 0x2008 : old.colorSpace;
		made.bytesPerRow = uint32_t(w) * 4;
		made.bounds[2] = float(w - 1);
		made.bounds[3] = float(h - 1);
		made.bits.resize(size_t(made.bytesPerRow) * size_t(h));
		const uint8_t* back = edit->colors[1];
		for (size_t i = 0; i < made.bits.size(); i += 4) {
			made.bits[i] = back[2];
			made.bits[i + 1] = back[1];
			made.bits[i + 2] = back[0];
			made.bits[i + 3] = back[3];
		}
		int rows = std::min(h, old.Height()), columns = std::min(w, old.Width());
		for (int y = 0; y < rows && !old.bits.empty(); y++)
			std::copy(old.bits.begin() + long(size_t(y) * old.bytesPerRow),
				old.bits.begin() + long(size_t(y) * old.bytesPerRow + size_t(columns) * 4),
				made.bits.begin() + long(size_t(y) * made.bytesPerRow));
		edit->image = std::move(made);
	}
	edit->size[0] = width;
	edit->size[1] = height;
	edit->rect[0] = edit->rect[1] = 0;
	edit->rect[2] = width - 1;
	edit->rect[3] = height - 1;
	// (ReplaceBitmap: the selection kept to the new size)
	edit->selection.Clip(w, h);
	// its frames (TFrame::SetSize; TFrameIM: the image's rectangle shown),
	// and for the document's own image its sheet's drawing's (the root's
	// stays: TBrowseRootFrame draws light gray around a smaller image)
	std::vector<size_t> framed = {part};
	if (doc.MainPart() == int(part) && !doc.sheets.sheets.empty()
			&& doc.sheets.sheets[0].graphicsPart < doc.tails.size())
		framed.push_back(doc.sheets.sheets[0].graphicsPart);
	for (size_t p : framed) {
		if (p >= doc.tails.size() || !doc.tailRead[p])
			continue;
		for (Portal& portal : doc.tails[p].portals)
			for (Frame& f : portal.frames) {
				f.extent[0] = width;
				f.extent[1] = height;
				f.sheetRect[0] = f.sheetRect[1] = 0;
				f.sheetRect[2] = width;
				f.sheetRect[3] = height;
			}
		doc.dirtyTails.insert(p);
	}
	return true;
}

} // namespace gobe
