#include "ResourceFile.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace gobe {

namespace {

const uint32_t kResourcesHeaderMagic = 0x444f1000;
const uint32_t kIndexHeaderSize = 132;
const uint32_t kIndexEntrySize = 12;

uint32_t U32(const std::vector<uint8_t>& d, size_t at)
{
	if (at + 4 > d.size())
		return 0;
	return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16
		| uint32_t(d[at + 3]) << 24;
}

uint16_t U16(const std::vector<uint8_t>& d, size_t at)
{
	if (at + 2 > d.size())
		return 0;
	return uint16_t(d[at] | d[at + 1] << 8);
}

} // namespace


std::string
FourCCString(uint32_t code)
{
	std::string s;
	for (int shift = 24; shift >= 0; shift -= 8) {
		char c = char((code >> shift) & 0xff);
		if (c)
			s += c;
	}
	return s;
}


bool
ResourceFile::Load(const std::string& path)
{
	fResources.clear();
	fIndex.clear();

	std::ifstream in(path, std::ios::binary);
	if (!in) {
		fError = "cannot open " + path;
		return false;
	}
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	if (d.size() < 0x34 || memcmp(d.data(), "\x7f" "ELF", 4) != 0 || d[4] != 1) {
		fError = path + " is not a 32-bit ELF file";
		return false;
	}

	// Resources start after the section header table, aligned to 32 bytes.
	uint32_t shoff = U32(d, 0x20);
	uint32_t shentsize = U16(d, 0x2e);
	uint32_t shnum = U16(d, 0x30);
	size_t base = (size_t(shoff) + shentsize * shnum + 31) & ~size_t(31);
	while (base + 4 <= d.size() && U32(d, base) != kResourcesHeaderMagic)
		base += 32;
	if (base + 4 > d.size()) {
		fError = path + " has no resources";
		return false;
	}

	uint32_t indexOffset = U32(d, base + 8);
	size_t ih = base + indexOffset;
	uint32_t indexSize = U32(d, ih + 4);
	uint32_t infoOffset = U32(d, ih + 30 * 4);
	uint32_t infoSize = U32(d, ih + 31 * 4);
	uint32_t slots = (indexSize - kIndexHeaderSize) / kIndexEntrySize;

	size_t p = base + infoOffset;
	size_t end = p + infoSize - 8;	// table end: checksum + terminator
	while (p < end && p + 4 <= d.size()) {
		uint32_t type = U32(d, p);	// stored little-endian
		p += 4;
		while (p + 10 <= d.size()) {
			int32_t id = int32_t(U32(d, p));
			uint32_t index = U32(d, p + 4);	// 1-based slot
			uint16_t nameSize = U16(d, p + 8);
			p += 10;
			std::string name(reinterpret_cast<const char*>(&d[p]),
				strnlen(reinterpret_cast<const char*>(&d[p]), nameSize));
			p += nameSize;

			if (index >= 1 && index <= slots) {
				size_t entry = ih + kIndexHeaderSize + (index - 1) * kIndexEntrySize;
				uint32_t offset = U32(d, entry);
				uint32_t size = U32(d, entry + 4);
				if (base + offset + size <= d.size()) {
					Resource r;
					r.type = type;
					r.id = id;
					r.name = name;
					r.data.assign(d.begin() + base + offset,
						d.begin() + base + offset + size);
					fIndex[{type, id}] = fResources.size();
					fResources.push_back(std::move(r));
				}
			}

			// a separator (two 0xffffffff) ends this type's block
			if (U32(d, p) == 0xffffffff && U32(d, p + 4) == 0xffffffff) {
				p += 8;
				break;
			}
			if (p >= end)
				break;
		}
	}
	return true;
}


const ResourceFile::Resource*
ResourceFile::Find(uint32_t type, int32_t id) const
{
	auto it = fIndex.find({type, id});
	return it == fIndex.end() ? nullptr : &fResources[it->second];
}


std::vector<const ResourceFile::Resource*>
ResourceFile::All(uint32_t type) const
{
	std::vector<const Resource*> res;
	for (auto& [key, i] : fIndex)
		if (key.first == type)
			res.push_back(&fResources[i]);
	return res;
}

} // namespace gobe
