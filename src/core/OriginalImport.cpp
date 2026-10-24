#include "OriginalImport.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace gobe {

namespace {

const char kPadding[7] = {0, 0, 0, 0, 0, 0, 0};
const char kSignature[] = "application/x-vnd.gobe.productive";


// SoftwareValet's big-endian fields and 7-byte tags (PackageInfo.cpp)
class ValetReader {
public:
	ValetReader(const std::vector<uint8_t>& data, size_t base)
		: fData(data), fBase(base), fPos(base) {}

	void Seek(uint64_t relative) { fPos = fBase + relative; }
	size_t Position() const { return fPos; }
	void SetPosition(size_t position) { fPos = position; }
	bool AtEnd() const { return fPos >= fData.size(); }

	std::string Tag()
	{
		Need(7);
		std::string tag(reinterpret_cast<const char*>(&fData[fPos]), 7);
		fPos += 7;
		return tag;
	}

	uint32_t U32()
	{
		Need(4);
		uint32_t v = 0;
		for (int i = 0; i < 4; i++)
			v = v << 8 | fData[fPos++];
		return v;
	}

	uint64_t U64()
	{
		uint64_t high = U32();
		return high << 32 | U32();
	}

	const uint8_t* Raw(size_t n)
	{
		Need(n);
		const uint8_t* p = &fData[fPos];
		fPos += n;
		return p;
	}

	std::string String()
	{
		uint32_t n = U32();
		const uint8_t* p = Raw(n);
		return std::string(reinterpret_cast<const char*>(p), n);
	}

	// the next four bytes, without moving
	std::string Peek4() const
	{
		if (fPos + 4 > fData.size())
			return "";
		return std::string(reinterpret_cast<const char*>(&fData[fPos]), 4);
	}

private:
	void Need(size_t n) const
	{
		if (fPos + n > fData.size())
			throw std::runtime_error("the package ends early");
	}

	const std::vector<uint8_t>& fData;
	size_t fBase;
	size_t fPos;
};


bool
IsPadding(const std::string& tag)
{
	return tag.size() == 7 && memcmp(tag.data(), kPadding, 7) == 0;
}


bool
Inflate(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out)
{
	z_stream z;
	memset(&z, 0, sizeof z);
	if (inflateInit(&z) != Z_OK)
		return false;
	z.next_in = const_cast<Bytef*>(in);
	z.avail_in = uInt(inSize);
	uint8_t buffer[65536];
	int result;
	do {
		z.next_out = buffer;
		z.avail_out = sizeof buffer;
		result = inflate(&z, Z_NO_FLUSH);
		if (result != Z_OK && result != Z_STREAM_END) {
			inflateEnd(&z);
			return false;
		}
		out.insert(out.end(), buffer, buffer + (sizeof buffer - z.avail_out));
	} while (result != Z_STREAM_END && z.avail_in > 0);
	inflateEnd(&z);
	return true;
}


bool
ReadFile(const std::string& path, std::vector<uint8_t>& data, uint64_t offset = 0,
	uint64_t size = UINT64_MAX)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	if (size == UINT64_MAX) {
		in.seekg(0, std::ios::end);
		uint64_t length = uint64_t(in.tellg());
		if (offset > length)
			return false;
		size = length - offset;
	}
	data.resize(size_t(size));
	in.seekg(std::streamoff(offset));
	in.read(reinterpret_cast<char*>(data.data()), std::streamsize(size));
	return bool(in) || in.gcount() == std::streamsize(size);
}


bool
WriteFile(const fs::path& path, const std::vector<uint8_t>& data, std::string& error)
{
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
	if (!out) {
		error = "Couldn't write " + path.string();
		return false;
	}
	return true;
}


bool
HasValetPackage(const std::vector<uint8_t>& data)
{
	size_t base;
	std::vector<ValetItem> items;
	std::string error;
	return ReadValetPackage(data, base, items, error);
}


uint32_t
LittleU32(const uint8_t* p)
{
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}


// UCS-2 big-endian (Joliet) to UTF-8
std::string
FromUcs2(const uint8_t* p, size_t length)
{
	std::string out;
	for (size_t i = 0; i + 1 < length; i += 2) {
		uint32_t c = uint32_t(p[i]) << 8 | p[i + 1];
		if (c < 0x80)
			out += char(c);
		else if (c < 0x800) {
			out += char(0xc0 | c >> 6);
			out += char(0x80 | (c & 0x3f));
		} else {
			out += char(0xe0 | c >> 12);
			out += char(0x80 | (c >> 6 & 0x3f));
			out += char(0x80 | (c & 0x3f));
		}
	}
	return out;
}


// Extracts the package in `data` into `destination`; the program's path.
bool
ExtractPackage(const std::vector<uint8_t>& data, const fs::path& destination,
	std::string& program, std::string& error,
	const std::function<void(const std::string&)>& progress)
{
	size_t base;
	std::vector<ValetItem> items;
	if (!ReadValetPackage(data, base, items, error))
		return false;
	program.clear();
	for (const ValetItem& item : items) {
		fs::path target = destination / fs::path(item.path);
		std::error_code ec;
		switch (item.kind) {
			case ValetItem::kFolder:
				fs::create_directories(target, ec);
				break;
			case ValetItem::kFile:
			{
				if (item.offset == 0)
					break;
				std::vector<uint8_t> bytes;
				if (!ReadValetItem(data, base, item, bytes, error)
						|| !WriteFile(target, bytes, error))
					return false;
				if (item.mode != 0)
					fs::permissions(target, fs::perms((item.mode & 0777) | 0600), ec);
				if (program.empty() && target.filename() == "Gobe Productive"
						&& IsOriginalProgram(bytes))
					program = target.string();
				if (progress)
					progress(item.path);
				break;
			}
			case ValetItem::kLink:
				fs::create_directories(target.parent_path(), ec);
				if (!fs::exists(fs::symlink_status(target)))
					fs::create_symlink(item.link, target, ec);
				break;
			case ValetItem::kScript:
				// (the installer's scripts: not run, not kept)
				break;
		}
	}
	if (program.empty()) {
		error = "The package has no Gobe Productive program in it.";
		return false;
	}
	return true;
}


// Copies the folder that holds the program (from the file system) to
// `destination`.
bool
CopyProgramFolder(const fs::path& programFile, const fs::path& destination,
	std::string& program, std::string& error,
	const std::function<void(const std::string&)>& progress)
{
	fs::path folder = programFile.parent_path();
	fs::path target = destination / folder.filename();
	std::error_code ec;
	if (fs::equivalent(folder, target, ec)) {
		program = programFile.string();
		return true;
	}
	fs::create_directories(target, ec);
	for (auto it = fs::recursive_directory_iterator(folder, ec);
			it != fs::recursive_directory_iterator(); it.increment(ec)) {
		if (ec)
			break;
		fs::path to = target / fs::relative(it->path(), folder, ec);
		if (it->is_directory(ec))
			fs::create_directories(to, ec);
		else if (it->is_regular_file(ec)) {
			fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
			if (progress)
				progress(it->path().string());
		}
	}
	program = (target / programFile.filename()).string();
	if (!fs::exists(program)) {
		error = "Couldn't copy " + folder.string();
		return false;
	}
	return true;
}


// The same from an ISO image: the program's folder and what's under it.
bool
CopyIsoFolder(const std::string& image, const std::vector<IsoFile>& files,
	const IsoFile& programFile, const fs::path& destination, std::string& program,
	std::string& error, const std::function<void(const std::string&)>& progress)
{
	std::string folder = programFile.path.substr(0, programFile.path.rfind('/'));
	std::string name = folder.substr(folder.rfind('/') + 1);
	fs::path target = destination / (name.empty() ? "Gobe Productive" : name);
	for (const IsoFile& file : files) {
		if (file.path.compare(0, folder.size() + 1, folder + "/") != 0)
			continue;
		fs::path to = target / file.path.substr(folder.size() + 1);
		std::error_code ec;
		if (file.folder) {
			fs::create_directories(to, ec);
			continue;
		}
		std::vector<uint8_t> bytes;
		if (!ReadFile(image, bytes, file.offset, file.size) || !WriteFile(to, bytes, error))
			return false;
		fs::permissions(to, fs::perms(0755), ec);
		if (progress)
			progress(file.path);
	}
	program = (target / "Gobe Productive").string();
	return true;
}

} // namespace


bool
ReadValetPackage(const std::vector<uint8_t>& data, size_t& base,
	std::vector<ValetItem>& items, std::string& error)
{
	items.clear();
	// the installer's stub holds the magic as a string too: the header's
	// first tag must follow it
	static const uint8_t kMagic[4] = {'A', 'l', 'B', 0x1a};
	base = std::string::npos;
	for (size_t at = 0; at + 12 <= data.size(); at++) {
		at = size_t(std::search(data.begin() + at, data.end(), kMagic, kMagic + 4) - data.begin());
		if (at + 12 > data.size())
			break;
		if (memcmp(&data[at + 8], "PhIn", 4) == 0) {
			base = at;
			break;
		}
	}
	if (base == std::string::npos) {
		error = "No SoftwareValet package found.";
		return false;
	}
	try {
		ValetReader p(data, base);
		p.Raw(8);
		uint64_t contents = 0;
		while (true) {
			std::string tag = p.Tag();
			std::string key = tag.substr(0, 4);
			if (IsPadding(tag))
				break;
			if (key == "PhIn")
				continue;
			if (key == "FVer")
				p.Raw(4);
			else if (key == "AFla")
				p.Raw(8);
			else if (key == "FSiz" || key == "AOff")
				p.U64();
			else if (key == "COff")
				contents = p.U64();
			else
				throw std::runtime_error("unknown header tag " + key);
		}

		p.Seek(contents);
		std::vector<std::string> folders;
		bool inItem = false;
		ValetItem item;
		std::string name;
		while (!p.AtEnd()) {
			std::string tag = p.Tag();
			std::string key = tag.substr(0, 4);
			if (key == "FilI" || key == "FldI" || key == "LnkI" || key == "ScrI") {
				inItem = true;
				item = ValetItem();
				name.clear();
				item.kind = key == "FilI" ? ValetItem::kFile : key == "FldI" ? ValetItem::kFolder
					: key == "LnkI" ? ValetItem::kLink : ValetItem::kScript;
			} else if (key == "Name")
				name = p.String();
			else if (key == "Grps" || key == "Dest" || key == "Cust" || key == "Repl"
					|| key == "Plat" || key == "CTim" || key == "MTim")
				p.U32();
			else if (key == "Mode")
				item.mode = p.U32();
			else if (key == "OffT")
				item.offset = p.U64();
			else if (key == "OrgS")
				item.size = p.U64();
			else if (key == "CmpS")
				p.U64();
			else if (key == "Link")
				item.link = p.String();
			else if (key == "Mime" || key == "ASig")
				p.String();
			else if (key == "VrsI")
				p.Raw(24);
			else if (key == "FDat")
				;
			else if (IsPadding(tag)) {
				if (!inItem) {
					if (folders.empty())
						break;
					folders.pop_back();
					continue;
				}
				item.path.clear();
				for (const std::string& f : folders)
					item.path += f + "/";
				item.path += name;
				items.push_back(item);
				if (item.kind == ValetItem::kFolder)
					folders.push_back(name);
				inItem = false;
			} else if (key == "PkgA" || key == "PtcI")
				break;
			else
				throw std::runtime_error("unknown file tag " + key);
		}
	} catch (const std::exception& e) {
		error = std::string("The package doesn't read: ") + e.what();
		return false;
	}
	// (paths come from the package: none may leave the destination)
	for (const ValetItem& i : items)
		if (i.path.empty() || i.path[0] == '/' || i.path.find("..") != std::string::npos) {
			error = "The package has an unexpected path: " + i.path;
			return false;
		}
	return true;
}


bool
ReadValetItem(const std::vector<uint8_t>& data, size_t base, const ValetItem& item,
	std::vector<uint8_t>& out, std::string& error)
{
	out.clear();
	try {
		// FBeA (attributes) and FiDa (data) sections; padding outside an
		// attribute ends a section, and the block ends at padding not
		// followed by another section
		ValetReader p(data, base);
		p.Seek(item.offset);
		std::string section;
		bool inAttribute = false;
		while (true) {
			std::string tag = p.Tag();
			std::string key = tag.substr(0, 4);
			if (key == "FBeA" || key == "FiDa" || key == "FoDa") {
				section = key;
				continue;
			}
			if (IsPadding(tag) && !inAttribute) {
				std::string next = p.Peek4();
				if (next == "FBeA" || next == "FiDa")
					continue;
				break;
			}
			if (section == "FBeA") {
				if (key == "BeAI")
					inAttribute = true;
				else if (key == "BeAN")
					p.String();
				else if (key == "BeAT")
					p.U32();
				else if (key == "BeAD") {
					uint64_t compressed = p.U64();
					p.U64();
					p.Raw(4);
					p.Raw(size_t(compressed));
				} else if (IsPadding(tag))
					inAttribute = false;
				else
					throw std::runtime_error("unknown attribute tag " + key);
			} else if (section == "FiDa" && key == "FiMF") {
				uint64_t compressed = p.U64();
				p.U64();
				p.Raw(4);
				const uint8_t* bytes = p.Raw(size_t(compressed));
				if (!Inflate(bytes, size_t(compressed), out))
					throw std::runtime_error("bad compressed data");
			} else
				throw std::runtime_error("unexpected tag " + key);
		}
	} catch (const std::exception& e) {
		error = item.path + ": " + e.what();
		return false;
	}
	if (out.size() != item.size) {
		error = item.path + ": the data's size is wrong";
		return false;
	}
	return true;
}


bool
ReadIsoImage(const std::string& image, std::vector<IsoFile>& files, std::string& error)
{
	files.clear();
	std::ifstream in(image, std::ios::binary);
	if (!in) {
		error = "Couldn't open " + image;
		return false;
	}
	auto sector = [&](uint64_t lba, size_t count, std::vector<uint8_t>& out) {
		out.assign(count * 2048, 0);
		in.clear();
		in.seekg(std::streamoff(lba * 2048));
		in.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
		return in.gcount() == std::streamsize(out.size());
	};
	// the volume descriptors from sector 16: the primary one, and a Joliet
	// supplementary one (escape sequence %/@, %/C or %/E)
	std::vector<uint8_t> descriptor, root;
	bool primary = false, joliet = false;
	for (uint64_t lba = 16; lba < 64; lba++) {
		if (!sector(lba, 1, descriptor) || memcmp(&descriptor[1], "CD001", 5) != 0)
			break;
		uint8_t type = descriptor[0];
		if (type == 255)
			break;
		if (type == 1 && !primary && !joliet) {
			root.assign(descriptor.begin() + 156, descriptor.begin() + 190);
			primary = true;
		} else if (type == 2 && descriptor[88] == '%' && descriptor[89] == '/'
				&& (descriptor[90] == '@' || descriptor[90] == 'C' || descriptor[90] == 'E')) {
			root.assign(descriptor.begin() + 156, descriptor.begin() + 190);
			joliet = true;
		}
	}
	if (root.empty()) {
		error = "Not an ISO 9660 image.";
		return false;
	}

	struct Pending {
		std::string path;
		uint32_t lba, size;
	};
	std::vector<Pending> todo = {{"", LittleU32(&root[2]), LittleU32(&root[10])}};
	std::vector<uint32_t> visited;
	while (!todo.empty()) {
		Pending dir = todo.back();
		todo.pop_back();
		if (std::find(visited.begin(), visited.end(), dir.lba) != visited.end()
				|| dir.size > 64 * 1024 * 1024 || files.size() > 100000)
			continue;
		visited.push_back(dir.lba);
		std::vector<uint8_t> block;
		if (!sector(dir.lba, (dir.size + 2047) / 2048, block))
			continue;
		for (size_t at = 0; at < dir.size && at < block.size();) {
			uint8_t length = block[at];
			if (length == 0) {
				// records don't cross sectors: on to the next one
				at = (at / 2048 + 1) * 2048;
				continue;
			}
			if (at + length > block.size() || length < 34)
				break;
			const uint8_t* r = &block[at];
			uint8_t nameLength = r[32];
			bool folder = (r[25] & 2) != 0;
			std::string name;
			if (nameLength == 1 && (r[33] == 0 || r[33] == 1)) {
				at += length;
				continue;		// . and ..
			}
			if (joliet)
				name = FromUcs2(r + 33, nameLength);
			else {
				name.assign(reinterpret_cast<const char*>(r + 33), nameLength);
				// Rock Ridge: an NM entry in the system use area
				size_t su = 33 + nameLength + (nameLength % 2 == 0 ? 1 : 0);
				std::string rr;
				while (su + 4 <= length) {
					uint8_t entryLength = r[su + 2];
					if (entryLength < 4 || su + entryLength > length)
						break;
					if (r[su] == 'N' && r[su + 1] == 'M' && entryLength > 5)
						rr.append(reinterpret_cast<const char*>(r + su + 5), entryLength - 5);
					su += entryLength;
				}
				if (!rr.empty())
					name = rr;
			}
			size_t semicolon = name.find(';');
			if (semicolon != std::string::npos)
				name.resize(semicolon);
			if (!folder && !name.empty() && name.back() == '.')
				name.pop_back();
			IsoFile file;
			file.path = dir.path + "/" + name;
			file.offset = uint64_t(LittleU32(r + 2)) * 2048;
			file.size = LittleU32(r + 10);
			file.folder = folder;
			files.push_back(file);
			if (folder)
				todo.push_back({file.path, LittleU32(r + 2), LittleU32(r + 10)});
			at += length;
		}
	}
	return true;
}


bool
IsBfsImage(const std::string& image)
{
	// the superblock at 512 (+0x20 in a partition's first block): magic1
	// 'BFS1' after the 32-byte name
	std::vector<uint8_t> data;
	if (!ReadFile(image, data, 0, 4096))
		return false;
	for (size_t at : {size_t(512 + 32), size_t(32)})
		if (at + 4 <= data.size() && (memcmp(&data[at], "1SFB", 4) == 0
				|| memcmp(&data[at], "BFS1", 4) == 0))
			return true;
	return false;
}


bool
IsOriginalProgram(const std::vector<uint8_t>& data)
{
	if (data.size() < 64 || memcmp(data.data(), "\x7f" "ELF", 4) != 0)
		return false;
	const char* begin = reinterpret_cast<const char*>(data.data());
	const char* end = begin + data.size();
	size_t length = sizeof kSignature;		// with its NUL
	return std::search(begin, end, kSignature, kSignature + length) != end;
}


bool
ImportOriginal(const std::string& source, const std::string& destination,
	std::string& program, std::string& error,
	const std::function<void(const std::string&)>& progress)
{
	std::error_code ec;
	fs::path src(source), dest(destination);
	fs::create_directories(dest, ec);
	if (fs::is_directory(src, ec)) {
		// the program itself, an installer or package, or an image
		std::vector<fs::path> candidates;
		for (auto it = fs::recursive_directory_iterator(src,
					fs::directory_options::skip_permission_denied, ec);
				it != fs::recursive_directory_iterator(); it.increment(ec)) {
			if (ec)
				break;
			if (it->is_regular_file(ec))
				candidates.push_back(it->path());
		}
		// the program first, then installers, then images
		std::stable_sort(candidates.begin(), candidates.end(), [](const fs::path& a,
				const fs::path& b) {
			auto rank = [](const fs::path& p) {
				std::string n = p.filename().string();
				std::transform(n.begin(), n.end(), n.begin(), ::tolower);
				if (n == "gobe productive")
					return 0;
				if (n.find("install") != std::string::npos || n.find(".pkg") != std::string::npos)
					return 1;
				if (n.size() > 4 && n.compare(n.size() - 4, 4, ".iso") == 0)
					return 2;
				return 3;
			};
			return rank(a) < rank(b);
		});
		for (const fs::path& candidate : candidates) {
			if (fs::file_size(candidate, ec) > 2u * 1024 * 1024 * 1024)
				continue;
			std::string name = candidate.filename().string();
			std::string lower = name;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
			bool likely = lower == "gobe productive" || lower.find("install") != std::string::npos
				|| lower.find(".pkg") != std::string::npos
				|| (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".iso") == 0);
			if (!likely)
				continue;
			std::string sub;
			if (ImportOriginal(candidate.string(), destination, program, sub, progress))
				return true;
		}
		error = "No Gobe Productive program, installer or CD image was found in " + source + ".";
		return false;
	}

	// an ISO image: the program's folder, else an installer on it
	std::vector<IsoFile> files;
	std::string isoError;
	if (ReadIsoImage(source, files, isoError)) {
		for (const IsoFile& file : files) {
			if (file.folder || file.size > 64 * 1024 * 1024)
				continue;
			std::string leaf = file.path.substr(file.path.rfind('/') + 1);
			std::string lower = leaf;
			std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
			if (lower != "gobe productive")
				continue;
			std::vector<uint8_t> bytes;
			if (ReadFile(source, bytes, file.offset, file.size) && IsOriginalProgram(bytes))
				return CopyIsoFolder(source, files, file, dest, program, error, progress);
		}
		for (const IsoFile& file : files) {
			if (file.folder || file.size < 1024 || file.size > 512 * 1024 * 1024)
				continue;
			std::vector<uint8_t> bytes;
			if (!ReadFile(source, bytes, file.offset, file.size) || !HasValetPackage(bytes))
				continue;
			if (ExtractPackage(bytes, dest, program, error, progress))
				return true;
		}
		error = "The CD image has no Gobe Productive program or installer on it.";
		return false;
	}
	if (IsBfsImage(source)) {
		error = "This image holds a BeOS file system (BFS), which can't be read here yet. "
			"Copy the Gobe Productive installer or folder out of it (Haiku can mount it) "
			"and choose that.";
		return false;
	}

	std::vector<uint8_t> bytes;
	if (!ReadFile(source, bytes)) {
		error = "Couldn't read " + source;
		return false;
	}
	if (IsOriginalProgram(bytes))
		return CopyProgramFolder(src, dest, program, error, progress);
	if (HasValetPackage(bytes))
		return ExtractPackage(bytes, dest, program, error, progress);
	error = source + " isn't the Gobe Productive program, its installer or a CD image.";
	return false;
}

} // namespace gobe
