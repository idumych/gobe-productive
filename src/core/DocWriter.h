// Writing Gobe Productive documents (TOutputArchive in the original):
// big-endian, the same encodings DocArchive reads. SaveDocument rebuilds a
// document from its original bytes: sections that weren't changed are
// copied, the PartMgr section is written from the part records (edited
// parts replaced), and the table of contents and the header's TOC offset
// are updated.
#ifndef GOBE_DOC_WRITER_H
#define GOBE_DOC_WRITER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gobe {

struct Document;

class DocWriter {
public:
	void UInt8(uint8_t v) { fData.push_back(v); }
	void Bool(bool v) { UInt8(v ? 1 : 0); }
	void UInt16(uint16_t v);
	void UInt32(uint32_t v);
	void Float(float v);
	void Double(double v);
	void String(const std::string& s);	// TString: u16 length, bytes, NUL
	void ShortString(const std::string& s);	// formula strings: u16 + bytes
	void Bytes(const std::vector<uint8_t>& b);
	void Bytes(const uint8_t* b, size_t n);
	void Sig(uint32_t s) { UInt32(s); }
	void Point(const float p[2]) { Float(p[0]); Float(p[1]); }
	void Rect(const float r[4]) { for (int i = 0; i < 4; i++) Float(r[i]); }

	size_t Position() const { return fData.size(); }
	const std::vector<uint8_t>& Data() const { return fData; }
	std::vector<uint8_t>& Data() { return fData; }

private:
	std::vector<uint8_t> fData;
};

// replaced part records (part index -> record bytes from 'PART' to 'ParT')
// and replaced sections (TOC name -> section bytes)
struct DocChanges {
	std::map<size_t, std::vector<uint8_t>> parts;
	std::map<std::string, std::vector<uint8_t>> sections;
};

std::vector<uint8_t> SaveDocument(const Document& doc, const DocChanges& changes);
bool WriteFile(const std::string& path, const std::vector<uint8_t>& data,
	std::string& error);

} // namespace gobe

#endif
