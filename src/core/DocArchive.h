// Reading Gobe Productive documents (TInputArchive in the original).
//
// The archive is big-endian (TArchive always sets its swap flag and x86
// ByteSwap reverses bytes): u8/bool/char 1 byte, u16, u32, float, double.
// Object pointers are stored as u32 ids and fixed up after loading. Strings
// (operator>>(TInputArchive&, TString&)) are a u16 length, then the
// characters and their NUL (an empty string is the length and one NUL).
// Many structures are bracketed by four-char signatures (CheckSig), which
// throw on mismatch.
#ifndef GOBE_DOC_ARCHIVE_H
#define GOBE_DOC_ARCHIVE_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace gobe {

class ArchiveError : public std::runtime_error {
public:
	ArchiveError(const std::string& what, size_t position)
		: std::runtime_error(what), fPosition(position) {}
	size_t Position() const { return fPosition; }

private:
	size_t fPosition;
};


class DocArchive {
public:
	explicit DocArchive(std::vector<uint8_t> data);

	size_t Position() const { return fPos; }
	size_t Size() const { return fData.size(); }
	void Seek(size_t position);

	uint8_t UInt8();
	bool Bool() { return UInt8() != 0; }
	uint16_t UInt16();
	int16_t Int16() { return int16_t(UInt16()); }
	uint32_t UInt32();
	int32_t Int32() { return int32_t(UInt32()); }
	float Float();
	double Double();
	std::string String();				// TString
	std::vector<uint8_t> Bytes(size_t count);	// TInputArchive::Read
	void Rect(float rect[4]);			// TRect: 4 floats
	void Point(float point[2]);			// TPoint: 2 floats

	// CheckSig: throws ArchiveError if the next u32 isn't `signature`
	void CheckSig(uint32_t signature);

	// operator>>(BMessage*&): u32 size; if non-zero 'MESG', the flattened
	// message, 'MesG'. Returns the flattened bytes (empty for none).
	std::vector<uint8_t> Message();

	const std::vector<uint8_t>& Data() const { return fData; }

	// TInputArchive::MajorUserVersion / BuildVersion (from the header's
	// TVersion: values[0] and [3]); some readers depend on them. Sub-archives
	// over part records copy them with CopyVersions.
	void SetVersions(int major, int build, int format = 0)
		{ fMajor = major; fBuild = build; fFormat = format; }
	int MajorUserVersion() const { return fMajor; }
	int BuildVersion() const { return fBuild; }
	// TInputArchive::FileFormatVersion: the header TVersion's values[2]
	int FileFormatVersion() const { return fFormat; }
	void CopyVersions(const DocArchive& from)
		{ fMajor = from.fMajor; fBuild = from.fBuild; fFormat = from.fFormat; }

private:
	void Need(size_t count);

	std::vector<uint8_t> fData;
	size_t fPos = 0;
	int fMajor = 0;
	int fBuild = 0;
	int fFormat = 0;
};


// The fixed header (TArchiveHeader::Read): identity "GOBE SCOTT/01/02",
// file type ('DOC!'), 7 more u32s, a u16, a u32, a float, a double (the last
// three are byte-order test values), a TVersion and 16 more bytes: 98 bytes.
struct DocVersion {
	uint16_t values[8];
};

struct DocHeader {
	std::string identity;
	uint32_t fileType = 0;
	uint32_t fields[7] = {};
	DocVersion version{};
};

// TVersion::Read: eight u16 (the 4th is read last for versions < 16)
DocVersion ReadVersion(DocArchive& a);

// Table of contents written at the end of the file (TTableOfContents): the
// original never reads it back (loading is sequential), but it marks where
// each section starts. Entries: name (TString), offset u32, kind u32.
struct TocEntry {
	std::string name;
	uint32_t offset;
	uint32_t kind;
};

struct DocFile {
	DocHeader header;
	std::vector<TocEntry> toc;
	size_t tocOffset = 0;
};

// Reads the header and finds/reads the table of contents.
bool ReadDocFile(DocArchive& a, DocFile& file, std::string& error);

constexpr uint32_t Sig(const char (&s)[5])
{
	return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16)
		| (uint32_t(uint8_t(s[2])) << 8) | uint32_t(uint8_t(s[3]));
}

} // namespace gobe

#endif
