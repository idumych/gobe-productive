// TInkSet's color, pattern and gradient set resources (TRsrHelper:
// little-endian u32s, a color as four of them, floats as text), ink set
// files and a document's sets.
#include "InkSets.h"

#include <dirent.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "DocArchive.h"
#include "DocWriter.h"
#include "ResourceFile.h"

namespace gobe {

namespace {

struct Reader {
	const std::vector<uint8_t>& d;
	size_t at = 0;
	bool ok = true;

	uint32_t U32()
	{
		if (at + 4 > d.size()) {
			ok = false;
			return 0;
		}
		uint32_t v = d[at] | d[at + 1] << 8 | d[at + 2] << 16 | uint32_t(d[at + 3]) << 24;
		at += 4;
		return v;
	}

	Color GetColor()
	{
		Color c;
		c.r = uint8_t(U32());
		c.g = uint8_t(U32());
		c.b = uint8_t(U32());
		c.a = uint8_t(U32());
		return c;
	}

	// TRsrHelper::GetFloatStr: a NUL-terminated number, 0 if empty
	float FloatString()
	{
		std::string text;
		while (at < d.size() && d[at] != 0)
			text += char(d[at++]);
		if (at >= d.size()) {
			ok = false;
			return 0;
		}
		at++;
		return text.empty() ? 0 : float(atof(text.c_str()));
	}
};

} // namespace


bool
ParsePatternSet(const std::vector<uint8_t>& resource, InkSet& set)
{
	set = InkSet();
	if (resource.size() < 8 || (resource.size() - 8) % 64 != 0)
		return false;
	Reader r{resource};
	set.columns = int(r.U32());
	set.cell = int(r.U32());
	while (r.at < resource.size()) {
		Ink ink;
		ink.set = true;
		ink.color = r.GetColor();
		ink.low = r.GetColor();
		for (uint8_t& b : ink.pattern)
			b = uint8_t(r.U32());
		set.inks.push_back(ink);
	}
	return r.ok;
}


bool
ParseGradientSet(const std::vector<uint8_t>& resource, InkSet& set)
{
	set = InkSet();
	Reader r{resource};
	set.columns = int(r.U32());
	set.cell = int(r.U32());
	while (r.ok) {
		uint32_t type = r.U32();
		if (!r.ok || type == Sig("gEND"))
			break;
		Ink ink;
		ink.set = true;
		ink.gradient = true;
		ink.gradientType = type;
		ink.colorCount = int(r.U32());
		for (Color& c : ink.colors)
			c = r.GetColor();
		ink.color = ink.colors[0];
		// TGradient::SetAngle takes the u32 as a float
		ink.angle = float(int32_t(r.U32()));
		for (int i = 0; i < 4; i++)
			r.U32();		// a rectangle
		float x = r.FloatString(), y = r.FloatString();
		if (type == Sig("GDrl")) {
			ink.center[0] = x;
			ink.center[1] = y;
		}
		if (r.ok)
			set.inks.push_back(ink);
	}
	return r.ok && !set.inks.empty();
}

bool
ParseColorSet(const std::vector<uint8_t>& resource, InkSet& set)
{
	set = InkSet();
	if (resource.size() < 12 || (resource.size() - 12) % 12 != 0)
		return false;
	Reader r{resource};
	set.columns = std::max(1, int(r.U32()));
	set.cell = int(r.U32());
	set.gap = int(r.U32());
	while (r.at < resource.size()) {
		Ink ink;
		ink.set = true;
		ink.color.r = uint8_t(r.U32());
		ink.color.g = uint8_t(r.U32());
		ink.color.b = uint8_t(r.U32());
		ink.color.a = 255;
		ink.low = ink.color;
		set.inks.push_back(ink);
	}
	return r.ok;
}


namespace {

const uint32_t kSetFile = 0x53455446;		// 'SETF'
const size_t kHeaderSize = 98;

// a new file's archive header (TArchiveHeader::Write; as the original's
// sets have it: 'SETF', the byte-order values, version 2.0 0x1b)
std::vector<uint8_t>
NewHeader()
{
	static const uint8_t kHeader[kHeaderSize] = {
		0x47, 0x4f, 0x42, 0x45, 0x20, 0x53, 0x43, 0x4f, 0x54, 0x54, 0x2f, 0x30,
		0x31, 0x2f, 0x30, 0x32, 0x53, 0x45, 0x54, 0x46, 0xff, 0xff, 0xff, 0xff,
		0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
		0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08,
		0x01, 0x02, 0x11, 0x12, 0x13, 0x14, 0x70, 0x1e, 0xaa, 0xb4, 0x46, 0x03,
		0xd5, 0x56, 0x86, 0xff, 0xd7, 0x52, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1b,
		0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00,
	};
	return std::vector<uint8_t>(kHeader, kHeader + kHeaderSize);
}

} // namespace


void
ReadInkSet(DocArchive& a, InkSet& set)
{
	a.CheckSig(Sig("INKS"));
	set.name = a.String();
	uint32_t count = a.UInt32();
	set.columns = int(a.UInt32());
	set.cell = int(a.UInt32());
	set.kind = int(a.UInt32());
	set.gap = int(a.UInt32());
	set.inks.clear();
	for (uint32_t i = 0; i < count; i++) {
		Ink ink;
		ReadInk(a, ink);
		set.inks.push_back(ink);
	}
	a.CheckSig(Sig("InkS"));
}


std::vector<uint8_t>
InkSetBytes(const InkSet& set)
{
	DocWriter w;
	w.Sig(Sig("INKS"));
	w.String(set.name);
	w.UInt32(uint32_t(set.inks.size()));
	w.UInt32(uint32_t(set.columns));
	w.UInt32(uint32_t(set.cell));
	w.UInt32(uint32_t(set.kind));
	w.UInt32(uint32_t(set.gap));
	for (const Ink& ink : set.inks)
		w.Bytes(InkBytes(ink));
	w.Sig(Sig("InkS"));
	return w.Data();
}


bool
ReadInkSetFile(const std::vector<uint8_t>& file, InkSet& set, std::string& error)
{
	set = InkSet();
	if (file.size() < kHeaderSize + 12 || std::string(file.begin(), file.begin() + 16)
			!= "GOBE SCOTT/01/02") {
		error = "not a Gobe Productive file";
		return false;
	}
	DocArchive a(file);
	try {
		a.Seek(16);
		if (a.UInt32() != kSetFile) {
			error = "not an ink set";
			return false;
		}
		a.Seek(66);
		DocVersion v = ReadVersion(a);
		a.SetVersions(v.values[0], v.values[3], v.values[2]);
		a.Seek(kHeaderSize);
		uint32_t two = a.UInt32(), one = a.UInt32();
		int kind = int(a.UInt32());
		if (two != 2 || one != 1) {
			error = "an ink set file of another version";
			return false;
		}
		ReadInkSet(a, set);
		a.CheckSig(Sig("END!"));
		set.kind = kind;
	} catch (const ArchiveError& e) {
		error = e.what();
		return false;
	}
	set.header.assign(file.begin(), file.begin() + kHeaderSize);
	return true;
}


std::vector<uint8_t>
WriteInkSetFile(const InkSet& set)
{
	DocWriter w;
	w.Bytes(set.header.size() == kHeaderSize ? set.header : NewHeader());
	w.UInt32(2);
	w.UInt32(1);
	w.UInt32(uint32_t(set.kind));
	w.Bytes(InkSetBytes(set));
	w.Sig(Sig("END!"));
	return w.Data();
}


bool
LoadInkSetFile(const std::string& path, InkSet& set, std::string& error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		error = path + ": can't be opened";
		return false;
	}
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
	return ReadInkSetFile(data, set, error);
}


bool
SaveInkSetFile(const std::string& path, const InkSet& set, std::string& error)
{
	std::vector<uint8_t> data = WriteInkSetFile(set);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out || !out.write(reinterpret_cast<const char*>(data.data()), data.size())) {
		error = path + ": can't be written";
		return false;
	}
	return true;
}


std::vector<InkSetEntry>
AvailableInkSets(const ResourceFile* resources, const std::vector<std::string>& folders,
	bool load)
{
	std::vector<InkSetEntry> sets;
	if (resources != nullptr) {
		// TInkSetManager::LoadAppInkSets: "Standard" of each kind
		static const uint32_t kTypes[3] = {Sig("COLR"), Sig("BPAT"), Sig("GRAD")};
		for (int kind = 0; kind < 3; kind++) {
			const ResourceFile::Resource* r = resources->Find(kTypes[kind], 0);
			InkSetEntry e;
			bool ok = r != nullptr && (kind == kColorSet ? ParseColorSet(r->data, e.set)
				: kind == kPatternSet ? ParsePatternSet(r->data, e.set)
				: ParseGradientSet(r->data, e.set));
			if (!ok)
				continue;
			e.set.kind = kind;
			e.set.name = "Standard";
			if (kind != kColorSet)
				e.set.gap = e.set.cell < 6 ? 0 : 2;
			sets.push_back(std::move(e));
		}
	}
	for (const std::string& folder : folders) {
		std::vector<std::string> names;
		if (DIR* dir = opendir(folder.c_str())) {
			while (dirent* d = readdir(dir))
				if (d->d_name[0] != '.')
					names.push_back(d->d_name);
			closedir(dir);
		}
		std::sort(names.begin(), names.end());
		for (const std::string& name : names) {
			std::string path = folder + "/" + name;
			std::ifstream in(path, std::ios::binary);
			std::vector<uint8_t> data;
			if (load)
				data.assign(std::istreambuf_iterator<char>(in), {});
			else {
				// the header and the set's name and numbers suffice
				data.resize(4096);
				in.read(reinterpret_cast<char*>(data.data()), data.size());
				data.resize(size_t(in.gcount()));
			}
			InkSetEntry e;
			std::string error;
			if (load ? !ReadInkSetFile(data, e.set, error) : data.size() < kHeaderSize + 12)
				continue;
			if (!load) {
				try {
					DocArchive a(data);
					a.Seek(16);
					if (a.UInt32() != kSetFile)
						continue;
					a.Seek(kHeaderSize + 8);
					e.set.kind = int(a.UInt32());
					a.CheckSig(Sig("INKS"));
					e.set.name = a.String();
					a.UInt32();
					e.set.columns = int(a.UInt32());
					e.set.cell = int(a.UInt32());
					a.UInt32();
					e.set.gap = int(a.UInt32());
				} catch (const ArchiveError&) {
					continue;
				}
			}
			e.path = path;
			sets.push_back(std::move(e));
		}
	}
	return sets;
}


void
ReadSetMgr(DocArchive& a, SetMgr& sets)
{
	sets = SetMgr();
	a.CheckSig(Sig("SETM"));
	for (int kind = 0; kind < 3; kind++) {
		uint32_t count = a.UInt32();
		for (uint32_t i = 0; i < count; i++) {
			InkSet set;
			ReadInkSet(a, set);
			sets.sets[kind].push_back(std::move(set));
		}
	}
	a.CheckSig(Sig("SetM"));
}


std::vector<uint8_t>
WriteSetMgr(const SetMgr& sets)
{
	DocWriter w;
	w.Sig(Sig("SETM"));
	for (int kind = 0; kind < 3; kind++) {
		w.UInt32(uint32_t(sets.sets[kind].size()));
		for (const InkSet& set : sets.sets[kind])
			w.Bytes(InkSetBytes(set));
	}
	w.Sig(Sig("SetM"));
	return w.Data();
}

} // namespace gobe
