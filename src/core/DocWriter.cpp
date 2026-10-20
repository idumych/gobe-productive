#include "DocWriter.h"

#include <cstring>
#include <fstream>

#include "Document.h"

namespace gobe {

void
DocWriter::UInt16(uint16_t v)
{
	fData.push_back(v >> 8);
	fData.push_back(v & 0xff);
}


void
DocWriter::UInt32(uint32_t v)
{
	for (int shift = 24; shift >= 0; shift -= 8)
		fData.push_back((v >> shift) & 0xff);
}


void
DocWriter::Float(float v)
{
	uint32_t bits;
	memcpy(&bits, &v, 4);
	UInt32(bits);
}


void
DocWriter::Double(double v)
{
	uint64_t bits;
	memcpy(&bits, &v, 8);
	UInt32(uint32_t(bits >> 32));
	UInt32(uint32_t(bits));
}


void
DocWriter::String(const std::string& s)
{
	UInt16(uint16_t(s.size()));
	Bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
	UInt8(0);
}


void
DocWriter::ShortString(const std::string& s)
{
	UInt16(uint16_t(s.size()));
	Bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}


void
DocWriter::Bytes(const std::vector<uint8_t>& b)
{
	fData.insert(fData.end(), b.begin(), b.end());
}


void
DocWriter::Bytes(const uint8_t* b, size_t n)
{
	fData.insert(fData.end(), b, b + n);
}


std::vector<uint8_t>
SaveDocument(const Document& doc, const DocChanges& changes)
{
	const std::vector<uint8_t>& original = doc.bytes;
	const std::vector<TocEntry>& toc = doc.file.toc;
	DocWriter out;
	// the header and anything before the first section
	size_t first = toc.empty() ? doc.file.tocOffset : toc[0].offset;
	out.Bytes(original.data(), first);

	std::vector<TocEntry> newToc = toc;
	for (size_t s = 0; s < toc.size(); s++) {
		newToc[s].offset = uint32_t(out.Position());
		size_t end = s + 1 < toc.size() ? toc[s + 1].offset : doc.file.tocOffset;
		auto replaced = changes.sections.find(toc[s].name);
		if (replaced != changes.sections.end()) {
			out.Bytes(replaced->second);
		} else if (toc[s].name == "PartMgr" && !changes.parts.empty()) {
			// TPartMgr::WriteParts: the index, then each record with its size
			out.Sig(gobe::Sig("PARM"));
			out.UInt32(doc.parts.value14);
			out.UInt32(uint32_t(doc.parts.parts.size()));
			auto record = [&](size_t i) -> const std::vector<uint8_t>& {
				auto it = changes.parts.find(i);
				return it != changes.parts.end() ? it->second : doc.parts.parts[i].data;
			};
			for (size_t i = 0; i < doc.parts.parts.size(); i++) {
				out.UInt32(doc.parts.parts[i].kind);
				out.UInt32(uint32_t(record(i).size() + 4));
			}
			for (size_t i = 0; i < doc.parts.parts.size(); i++) {
				out.UInt32(uint32_t(record(i).size() + 4));
				out.Bytes(record(i));
			}
			out.Sig(gobe::Sig("ParM"));
		} else
			out.Bytes(original.data() + toc[s].offset, end - toc[s].offset);
	}

	// TTableOfContents::Write, 'END!'; the header holds the TOC's offset
	uint32_t tocOffset = uint32_t(out.Position());
	out.UInt32(uint32_t(newToc.size()));
	for (const TocEntry& e : newToc) {
		out.String(e.name);
		out.UInt32(e.offset);
		out.UInt32(e.kind);
	}
	out.Sig(gobe::Sig("END!"));
	// TDoc::Write patches the header in memory and writes it again: x86
	// builds stored that offset little-endian, older documents big-endian.
	// Keep the original's order.
	std::vector<uint8_t>& data = out.Data();
	const uint8_t* h = original.data() + 0x14;
	uint32_t oldLittle = h[0] | h[1] << 8 | h[2] << 16 | uint32_t(h[3]) << 24;
	bool little = oldLittle == doc.file.tocOffset;
	for (int i = 0; i < 4; i++)
		data[0x14 + i] = (tocOffset >> (little ? 8 * i : 24 - 8 * i)) & 0xff;
	return data;
}


bool
WriteFile(const std::string& path, const std::vector<uint8_t>& data, std::string& error)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file) {
		error = "cannot write " + path;
		return false;
	}
	file.write(reinterpret_cast<const char*>(data.data()), data.size());
	if (!file) {
		error = "error writing " + path;
		return false;
	}
	return true;
}

} // namespace gobe
