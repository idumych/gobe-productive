#include "DocWriter.h"

#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#ifdef __HAIKU__
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <fs_attr.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/xattr.h>
#endif

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


#ifdef __HAIKU__
// The old file's attributes (its type, icon position, ...) onto the new one
static void
CopyAttributes(const char* from, const char* to)
{
	int in = open(from, O_RDONLY), out = open(to, O_WRONLY);
	DIR* names = in >= 0 ? fs_fopen_attr_dir(in) : NULL;
	if (names != NULL && out >= 0) {
		while (dirent* entry = fs_read_attr_dir(names)) {
			attr_info info;
			if (fs_stat_attr(in, entry->d_name, &info) != 0 || info.size > 1 << 24)
				continue;
			std::vector<char> value(size_t(info.size));
			ssize_t got = fs_read_attr(in, entry->d_name, info.type, 0, value.data(), value.size());
			if (got >= 0)
				fs_write_attr(out, entry->d_name, info.type, 0, value.data(), size_t(got));
		}
	}
	if (names != NULL)
		fs_close_attr_dir(names);
	if (in >= 0)
		close(in);
	if (out >= 0)
		close(out);
}
#elif defined(__linux__)
// The old file's extended attributes onto the new one (Cosmoe keeps a
// file's attributes, its type among them, as user.* xattrs)
static void
CopyAttributes(const char* from, const char* to)
{
	int in = open(from, O_RDONLY), out = open(to, O_WRONLY);
	ssize_t length = in >= 0 && out >= 0 ? flistxattr(in, NULL, 0) : -1;
	std::vector<char> names(length > 0 ? size_t(length) : 0);
	if (length > 0)
		length = flistxattr(in, names.data(), names.size());
	for (ssize_t at = 0; at < length; at += ssize_t(strlen(&names[size_t(at)])) + 1) {
		const char* name = &names[size_t(at)];
		ssize_t size = fgetxattr(in, name, NULL, 0);
		if (size < 0 || size > 1 << 24)
			continue;
		std::vector<char> value(static_cast<size_t>(size));
		size = fgetxattr(in, name, value.data(), value.size());
		if (size >= 0)
			fsetxattr(out, name, value.data(), size_t(size), 0);
	}
	if (in >= 0)
		close(in);
	if (out >= 0)
		close(out);
}
#endif


// Writes a new file and renames it over the old one, so a failed write
// leaves the old file whole
bool
WriteFile(const std::string& path, const void* data, size_t size, std::string& error)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::path target = fs::weakly_canonical(path, ec);	// (through a link, to the file itself)
	if (ec)
		target = path;
	std::string temporary = target.string() + ".saving";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		if (!file) {
			error = "cannot write " + path;
			return false;
		}
		file.write(static_cast<const char*>(data), std::streamsize(size));
		file.close();
		if (!file) {
			error = "error writing " + path;
			std::remove(temporary.c_str());
			return false;
		}
	}
	if (fs::exists(target, ec)) {
		fs::permissions(temporary, fs::status(target, ec).permissions(), ec);
#if defined(__HAIKU__) || defined(__linux__)
		CopyAttributes(target.c_str(), temporary.c_str());
#endif
	}
	if (std::rename(temporary.c_str(), target.c_str()) != 0) {
		error = "cannot write " + path;
		std::remove(temporary.c_str());
		return false;
	}
	return true;
}


bool
WriteFile(const std::string& path, const std::vector<uint8_t>& data, std::string& error)
{
	return WriteFile(path, data.data(), data.size(), error);
}

} // namespace gobe
