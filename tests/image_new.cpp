// image_new: File > New for image processing (CreateBlankImage). The
// document has the parts the original's new image document has (the image,
// the sheet's drawing, a browse root, all 512 by 512), the image white in
// B_RGBA32, black on white, its selection empty; it saves and reads back.
// With the original's own new image document at
// ../original/blanks/imgblank, the sections are compared with it byte for
// byte: all but the window and sheet records (the port writes its own, as
// for every document), the selection (its mask compressed by another zlib)
// and two places where the original wrote what was in memory.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Document.h"
#include "IMPart.h"

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


static std::vector<uint8_t>
Section(const Document& doc, const std::string& name)
{
	for (const TocEntry& e : doc.file.toc) {
		if (e.name != name)
			continue;
		size_t end = doc.file.tocOffset;
		for (const TocEntry& o : doc.file.toc)
			if (o.offset > e.offset && o.offset < end)
				end = o.offset;
		return std::vector<uint8_t>(doc.bytes.begin() + e.offset, doc.bytes.begin() + end);
	}
	return {};
}


int
main(int argc, char** argv)
{
	Document doc;
	std::string error;
	Check(CreateBlankImage(doc, error), "created: " + error);
	Check(doc.parts.parts.size() == 3 && doc.parts.parts[0].kind == Sig("BITS")
		&& doc.parts.parts[1].kind == Sig("GRFX") && doc.parts.parts[2].kind == Sig("BROT"),
		"its parts: the image, the drawing, a browse root");
	Check(doc.RootPart() == 2, "the browse root is the root");
	int frames = 0;
	for (const Document::PlacedFrame& f : doc.PlacedFrames()) {
		frames++;
		Check(f.x == 0 && f.y == 0 && f.width == 512 && f.height == 512, "a frame 512 by 512");
	}
	Check(frames == 3, "three frames");
	const IMPart* image = doc.ImagePart(0);
	Check(image != nullptr, "the image part read");
	if (image != nullptr) {
		Check(image->size[0] == 512 && image->size[1] == 512, "512 by 512");
		Check(image->image.colorSpace == 0x2008 && image->image.Width() == 512
			&& image->image.Height() == 512, "a B_RGBA32 bitmap 512 by 512");
		bool white = !image->image.bits.empty();
		for (uint8_t b : image->image.bits)
			white = white && b == 0xff;
		Check(white, "white");
		Check(image->floating.Empty(), "nothing floating");
		Check(image->colors[0][0] == 0 && image->colors[0][3] == 0xff
			&& image->colors[1][0] == 0xff, "black on white");
	}
	std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp")
		+ "/image_new.gobe";
	Check(doc.Save(path, error), "saved: " + error);
	Document back;
	Check(LoadDocument(path, back, error), "read back: " + error);
	Check(back.ImagePart(0) != nullptr && back.ImagePart(0)->image.bits.size() == 512 * 512 * 4,
		"its image after saving");

	// Document Size… (TIMSetPartSizeCommand): smaller, then larger with the
	// background color around the old image
	Check(SetImageSize(doc, 0, 300, 200), "resized to 300 by 200");
	Check(!SetImageSize(doc, 0, 300, 200), "the same size again: nothing");
	for (const Document::PlacedFrame& f : doc.PlacedFrames())
		Check(doc.parts.parts[f.part].kind == Sig("BROT") ? f.width == 512 && f.height == 512
			: f.width == 300 && f.height == 200, "its frames 300 by 200, the root's as it was");
	IMPart* edit = doc.EditableImagePart(0);
	edit->image.bits[0] = 0x10;		// a pixel at the top left, blue
	edit->colors[1][0] = 0x20;		// the background (TColor: red first)
	edit->colors[1][1] = 0x40;
	edit->colors[1][2] = 0x60;
	Check(SetImageSize(doc, 0, 400, 500), "resized to 400 by 500");
	const IMPart* grown = doc.ImagePart(0);
	Check(grown->image.Width() == 400 && grown->image.Height() == 500
		&& grown->image.bits.size() == 400 * 500 * 4, "a bitmap 400 by 500");
	Check(grown->image.bits[0] == 0x10, "the old image at the top left");
	size_t corner = (size_t(499) * 400 + 399) * 4;
	Check(grown->image.bits[corner] == 0x60 && grown->image.bits[corner + 1] == 0x40
		&& grown->image.bits[corner + 2] == 0x20, "the rest the background color");
	size_t edge = (size_t(10) * 400 + 300) * 4;
	Check(grown->image.bits[edge] == 0x60, "past the old width the background too");
	Check(doc.Save(path, error), "resized saved: " + error);
	Document resized;
	Check(LoadDocument(path, resized, error) && resized.ImagePart(0) != nullptr
		&& resized.ImagePart(0)->size[0] == 400 && resized.ImagePart(0)->image.Height() == 500,
		"resized read back");
	int resizedFrames = 0;
	for (const Document::PlacedFrame& f : resized.PlacedFrames()) {
		resizedFrames++;
		Check(resized.parts.parts[f.part].kind == Sig("BROT") || (f.width == 400 && f.height == 500),
			"its frames read back 400 by 500");
	}
	Check(resizedFrames == 3, "three frames read back");

	Document original;
	Document fresh;
	CreateBlankImage(fresh, error);
	if (LoadDocument("../original/blanks/imgblank", original, error)) {
		for (const char* name : {"VersionMgr", "PartFactory Table", "Document Misc", "SetMgr",
				"PenMgr", "StyleMgr", "LinksMgr", "CommandMgr", "HistoryMgr"})
			Check(Section(fresh, name) == Section(original, name), std::string(name) + " as the original's");
		std::vector<uint8_t> ours = Section(fresh, "PartMgr"), theirs = Section(original, "PartMgr");
		size_t differ = 0;
		for (size_t i = 0; i < ours.size() && i < theirs.size(); i++)
			differ += ours[i] != theirs[i];
		Check(ours.size() == theirs.size() && differ <= 8,
			"the parts as the original's but for what it had in memory ("
				+ std::to_string(differ) + " bytes)");
	} else
		printf("note: no ../original/blanks/imgblank: not compared with the original\n");
	printf("image_new: ok %d, failed %d\n", sOk, sFailed);
	return sFailed == 0 ? 0 : 1;
}
