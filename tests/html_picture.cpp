// html_picture: pictures in HTML (TExportHTML::writeBitmap). LogoLetter's
// logo, an image part, is put in a new document's text (the samples have
// pictures only in headers and drawings, which the export doesn't
// write); it must come out as an <IMG> tag and a PNG file: its chunks
// have the right CRCs, its size is the frame's rectangle of the bitmap,
// and inflated it holds the bitmap's pixels (B, G, R, A turned to R, G,
// B, A). Without a list for the pictures no <IMG> is written, and the
// documents given write none (their pictures aren't in their main text).
// Arguments: documents (LogoLetter's logo is used).
#include <cstdio>
#include <string>
#include <vector>

#include <zlib.h>

#include "Document.h"
#include "Editing.h"
#include "HTMLExport.h"
#include "PNG.h"
#include "RTF.h"

using namespace gobe;

static int sOk = 0, sFailed = 0;

static void
Check(bool ok, const std::string& what)
{
	if (ok)
		sOk++;
	else {
		sFailed++;
		printf("FAIL %s\n", what.c_str());
	}
}


static uint32_t
Big32(const std::vector<uint8_t>& d, size_t at)
{
	return uint32_t(d[at]) << 24 | d[at + 1] << 16 | d[at + 2] << 8 | d[at + 3];
}


// the PNG's chunks (CRCs checked), its size and its pixels inflated
static bool
ReadPNG(const std::vector<uint8_t>& png, int& width, int& height, std::vector<uint8_t>& raw)
{
	static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	if (png.size() < 8 || !std::equal(kSignature, kSignature + 8, png.begin()))
		return false;
	std::vector<uint8_t> deflated;
	for (size_t at = 8; at + 12 <= png.size();) {
		uint32_t length = Big32(png, at);
		std::string type(png.begin() + long(at + 4), png.begin() + long(at + 8));
		if (at + 12 + length > png.size())
			return false;
		if (crc32(0, &png[at + 4], uInt(length + 4)) != Big32(png, at + 8 + length))
			return false;
		if (type == "IHDR") {
			width = int(Big32(png, at + 8));
			height = int(Big32(png, at + 12));
			if (png[at + 16] != 8 || png[at + 17] != 6)
				return false;
		} else if (type == "IDAT")
			deflated.insert(deflated.end(), png.begin() + long(at + 8),
				png.begin() + long(at + 8 + length));
		at += 12 + length;
	}
	uLongf size = uLongf(size_t(width) * size_t(height) * 4 + size_t(height));
	raw.resize(size);
	return uncompress(raw.data(), &size, deflated.data(), uLong(deflated.size())) == Z_OK
		&& size == raw.size();
}


int
main(int argc, char** argv)
{
	size_t pictures = 0;
	const IMPart* logo = nullptr;
	std::vector<Document> documents(static_cast<size_t>(argc));
	for (int i = 1; i < argc; i++) {
		Document& doc = documents[size_t(i)];
		std::string error;
		if (!LoadDocument(argv[i], doc, error))
			continue;
		std::vector<HTMLImage> images;
		std::string html = HTMLText(doc, "page", &images);
		Check(images.empty() && html.find("<IMG") == std::string::npos, std::string(argv[i])
			+ ": no pictures in the main text");
		for (const auto& [part, im] : doc.imageParts)
			if (logo == nullptr && im.image.colorSpace == 0x2008 && !im.image.Empty())
				logo = &im;
	}
	Check(logo != nullptr, "an RGBA32 image part among the documents");
	if (logo != nullptr) {
		Document doc;
		std::string error;
		Check(CreateBlankDocument(doc, error), "a new document " + error);
		size_t main = size_t(doc.MainPart());
		size_t I = doc.parts.parts.size(), D = I + 1;
		doc.parts.parts.resize(D + 1, PartRecord{Sig("BITS"), 0, {}});
		doc.tails.resize(D + 1);
		doc.tailRead.resize(D + 1, true);
		doc.imageParts.emplace_back(I, *logo);
		Portal holder;
		Frame whole, part;
		whole.part = part.part = uint32_t(I);
		// the second shows the rectangle (10, 20) to (49, 39) of it
		part.sheetRect[0] = 10;
		part.sheetRect[1] = 20;
		part.sheetRect[2] = 49;
		part.sheetRect[3] = 39;
		holder.frames = {whole, part};
		doc.tails[D].portals = {holder};
		WPPart* wp = doc.EditableWordPart(main);
		InsertMultiline(*wp, {0, 0}, "\x04 and \x04");
		for (uint32_t k = 0; k < 2; k++) {
			WPMiscInfo frame;
			frame.kind = Sig("FRAM");
			frame.position = k * 6;
			frame.frame[0] = uint32_t(D);
			frame.frame[1] = k;
			wp->paragraphs[0].misc.push_back(frame);
		}
		std::vector<HTMLImage> images;
		std::string html = HTMLText(doc, "page", &images);
		Check(HTMLText(doc, "page").find("<IMG") == std::string::npos,
			"no <IMG> without a list for the pictures");
		Check(images.size() == 2, "two pictures: " + std::to_string(images.size()));
		const int sizes[2][2] = {{logo->image.Width(), logo->image.Height()}, {40, 20}};
		const int origins[2][2] = {{0, 0}, {10, 20}};
		for (size_t k = 0; k < images.size() && k < 2; k++) {
			pictures++;
			std::string what = images[k].name;
			Check(images[k].name == "image." + std::to_string(k) + ".png", what + ": its name");
			int width = 0, height = 0;
			std::vector<uint8_t> raw;
			Check(ReadPNG(images[k].bytes, width, height, raw), what + ": a PNG");
			Check(width == sizes[k][0] && height == sizes[k][1], what + ": "
				+ std::to_string(width) + " x " + std::to_string(height));
			std::string tag = "<IMG SRC=\"page.images/" + images[k].name + "\" WIDTH=\""
				+ std::to_string(sizes[k][0]) + "\" HEIGHT=\"" + std::to_string(sizes[k][1]) + "\">";
			Check(html.find(tag) != std::string::npos, what + ": " + tag + " in\n" + html);
			if (raw.empty() || width != sizes[k][0] || height != sizes[k][1])
				continue;
			bool same = true;
			for (int y = 0; same && y < height; y++) {
				same = raw[size_t(y) * (size_t(width) * 4 + 1)] == 0;	// filter: none
				for (int x = 0; same && x < width; x++) {
					const uint8_t* p = &raw[size_t(y) * (size_t(width) * 4 + 1) + 1 + size_t(x) * 4];
					const uint8_t* b = &logo->image.bits[size_t(y + origins[k][1])
						* logo->image.bytesPerRow + size_t(x + origins[k][0]) * 4];
					same = p[0] == b[2] && p[1] == b[1] && p[2] == b[0] && p[3] == b[3];
				}
			}
			Check(same, what + ": the bitmap's pixels");
		}
		Check(html.find("<P><IMG SRC=\"page.images/image.0.png\"") != std::string::npos,
			"the picture in its paragraph");

		// RTF (TExportRTF::writeBitmap): {\pict\pngblip\picwW\pichH hex}
		FontCatalog fonts;
		fonts.fonts = {{"Dutch801 Rm BT", "Roman", 0}};
		fonts.plainFamily = "Dutch801 Rm BT";
		fonts.plainStyle = "Roman";
		std::string rtf = ExportedRTF(doc, main, fonts);
		size_t at = 0;
		for (size_t k = 0; k < images.size() && k < 2; k++) {
			std::string head = "{\\pict\\pngblip\\picw" + std::to_string(sizes[k][0]) + "\\pich"
				+ std::to_string(sizes[k][1]) + " ";
			at = rtf.find(head, at);
			Check(at != std::string::npos, "RTF picture " + std::to_string(k) + ": " + head);
			if (at == std::string::npos)
				break;
			at += head.size();
			size_t end = rtf.find('}', at);
			std::vector<uint8_t> bytes;
			for (size_t i = at; end != std::string::npos && i + 1 < end; i += 2)
				bytes.push_back(uint8_t(std::stoi(rtf.substr(i, 2), nullptr, 16)));
			Check(bytes == images[k].bytes, "RTF picture " + std::to_string(k) + ": the PNG");
			at = end;
		}
	}
	// a cropped B_RGB32 bitmap: 2 x 1 of a 3 x 2 one, opaque
	ArchiveBitmap bitmap;
	bitmap.colorSpace = 0x0008;
	bitmap.bytesPerRow = 12;
	bitmap.bounds[2] = 2;
	bitmap.bounds[3] = 1;
	for (int i = 0; i < 24; i++)
		bitmap.bits.push_back(uint8_t(i));
	const float crop[4] = {1, 1, 2, 1};
	int width = 0, height = 0;
	std::vector<uint8_t> raw;
	Check(ReadPNG(BitmapPNG(bitmap, crop), width, height, raw) && width == 2 && height == 1
		&& raw == std::vector<uint8_t>{0, 18, 17, 16, 255, 22, 21, 20, 255}, "a cropped RGB32 bitmap");

	printf("pictures %zu\n", pictures);
	printf("html_picture: ok %d, failed %d\n", sOk, sFailed);
	return sFailed || pictures == 0 ? 1 : 0;
}
