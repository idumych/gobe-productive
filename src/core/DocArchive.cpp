#include "DocArchive.h"

#include <cstring>

#include "ResourceFile.h"

namespace gobe {

DocArchive::DocArchive(std::vector<uint8_t> data)
	:
	fData(std::move(data))
{
}


void
DocArchive::Seek(size_t position)
{
	if (position > fData.size())
		throw ArchiveError("seek past end", position);
	fPos = position;
}


void
DocArchive::Need(size_t count)
{
	if (fPos + count > fData.size())
		throw ArchiveError("read past end", fPos);
}


uint8_t
DocArchive::UInt8()
{
	Need(1);
	return fData[fPos++];
}


uint16_t
DocArchive::UInt16()
{
	Need(2);
	uint16_t v = uint16_t(fData[fPos] << 8 | fData[fPos + 1]);
	fPos += 2;
	return v;
}


uint32_t
DocArchive::UInt32()
{
	Need(4);
	uint32_t v = uint32_t(fData[fPos]) << 24 | uint32_t(fData[fPos + 1]) << 16
		| uint32_t(fData[fPos + 2]) << 8 | uint32_t(fData[fPos + 3]);
	fPos += 4;
	return v;
}


float
DocArchive::Float()
{
	uint32_t bits = UInt32();
	float f;
	memcpy(&f, &bits, 4);
	return f;
}


double
DocArchive::Double()
{
	uint64_t hi = UInt32();
	uint64_t lo = UInt32();
	uint64_t bits = hi << 32 | lo;
	double d;
	memcpy(&d, &bits, 8);
	return d;
}


std::string
DocArchive::String()
{
	uint16_t length = UInt16();
	if (length == 0) {
		UInt8();	// the NUL
		return std::string();
	}
	Need(size_t(length) + 1);
	std::string s(reinterpret_cast<const char*>(&fData[fPos]), length);
	fPos += size_t(length) + 1;
	return s;
}


std::vector<uint8_t>
DocArchive::Bytes(size_t count)
{
	Need(count);
	std::vector<uint8_t> v(fData.begin() + fPos, fData.begin() + fPos + count);
	fPos += count;
	return v;
}


void
DocArchive::Rect(float rect[4])
{
	for (int i = 0; i < 4; i++)
		rect[i] = Float();
}


void
DocArchive::Point(float point[2])
{
	point[0] = Float();
	point[1] = Float();
}


void
DocArchive::CheckSig(uint32_t signature)
{
	size_t at = fPos;
	uint32_t v = UInt32();
	if (v != signature) {
		throw ArchiveError("expected signature '" + FourCCString(signature)
			+ "', found '" + FourCCString(v) + "'", at);
	}
}


std::vector<uint8_t>
DocArchive::Message()
{
	uint32_t size = UInt32();
	if (size == 0)
		return {};
	CheckSig(Sig("MESG"));
	std::vector<uint8_t> flat = Bytes(size);
	CheckSig(Sig("MesG"));
	return flat;
}


DocVersion
ReadVersion(DocArchive& a)
{
	DocVersion v{};
	v.values[0] = a.UInt16();
	v.values[1] = a.UInt16();
	v.values[2] = a.UInt16();
	if (v.values[2] > 0xf)
		v.values[3] = a.UInt16();
	v.values[4] = a.UInt16();
	v.values[5] = a.UInt16();
	v.values[6] = a.UInt16();
	v.values[7] = a.UInt16();
	if (v.values[2] < 0x10) {
		a.UInt16();
		v.values[3] = 4;
	}
	return v;
}


bool
ReadDocFile(DocArchive& a, DocFile& file, std::string& error)
{
	try {
		a.Seek(0);
		std::vector<uint8_t> id = a.Bytes(16);
		file.header.identity.assign(id.begin(), id.end());
		if (file.header.identity != "GOBE SCOTT/01/02") {
			error = "not a Gobe Productive document";
			return false;
		}
		file.header.fileType = a.UInt32();
		for (uint32_t& f : file.header.fields)
			f = a.UInt32();
		a.UInt16();
		a.UInt32();
		a.Float();
		a.Double();
		file.header.version = ReadVersion(a);
		a.SetVersions(file.header.version.values[0], file.header.version.values[3],
			file.header.version.values[2]);
		a.Bytes(16);

		// the TOC: the last "\0\x0aVersionMgr\0" preceded by the entry count
		static const char kFirst[] = "\0\x0aVersionMgr";
		const std::vector<uint8_t>& d = a.Data();
		size_t found = std::string::npos;
		for (size_t i = d.size() >= 13 ? d.size() - 13 : 0; i > 4; i--) {
			if (memcmp(&d[i], kFirst, 13) == 0) {
				found = i - 4;
				break;
			}
		}
		if (found == std::string::npos) {
			error = "no table of contents";
			return false;
		}
		size_t keep = a.Position();
		a.Seek(found);
		file.tocOffset = found;
		uint32_t count = a.UInt32();
		for (uint32_t i = 0; i < count; i++) {
			TocEntry e;
			e.name = a.String();
			e.offset = a.UInt32();
			e.kind = a.UInt32();
			file.toc.push_back(e);
		}
		a.Seek(keep);
	} catch (const ArchiveError& e) {
		error = e.what();
		return false;
	}
	return true;
}

} // namespace gobe
