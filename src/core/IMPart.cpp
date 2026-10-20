#include "IMPart.h"

#include <zlib.h>

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

	a.CheckSig(Sig("imp+"));
	a.Point(out.size);
	ReadBitmap(a, out.image);
	ReadBitmap(a, out.floating);
	a.Rect(out.rect);
	for (auto& c : out.colors)
		for (uint8_t& v : c)
			v = a.UInt8();
	a.CheckSig(Sig("imp-"));
}

} // namespace gobe
