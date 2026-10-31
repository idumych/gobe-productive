// Import Styles and Export Styles (TStyleManager::DoImportStyles,
// DoExportStyles): a styles file is a TOutputArchive of type 'STYL'
// holding one BMessage, a "Style Definition" per style as TStyle::Archive
// writes it. The messages are kept here as plain trees; the app turns them
// into BMessages and flattens them.
#ifndef GOBE_STYLE_ARCHIVE_H
#define GOBE_STYLE_ARCHIVE_H

#include <cstdint>
#include <string>
#include <vector>

#include "StyleEdit.h"

namespace gobe {

// a BMessage's contents: fields of one type, each with its values
struct ArchiveMessage {
	struct Value {
		int32_t integer = 0;		// int32, int16, bool
		float numbers[4] = {};		// float (numbers[0]), point (2), rect (4)
		std::string bytes;			// string (without its NUL), data
		std::vector<ArchiveMessage> message;	// a message: one element
	};
	struct Field {
		std::string name;
		uint32_t type = 0;			// B_INT32_TYPE ('LONG'), B_FLOAT_TYPE, ...
		std::vector<Value> values;
	};
	uint32_t what = 0;
	std::vector<Field> fields;

	void AddInt32(const std::string& name, int32_t value);
	void AddInt16(const std::string& name, int16_t value);
	void AddBool(const std::string& name, bool value);
	void AddFloat(const std::string& name, float value);
	void AddString(const std::string& name, const std::string& value);
	void AddData(const std::string& name, uint32_t type, const std::string& data);
	void AddPoint(const std::string& name, float x, float y);
	void AddRect(const std::string& name, const float rect[4]);
	void AddMessage(const std::string& name, const ArchiveMessage& message);

	// the field's `index`th value of `type`; NULL if there's none
	const Value* Find(const std::string& name, uint32_t type, size_t index = 0) const;
	size_t Count(const std::string& name) const;

private:
	void AddValue(const std::string& name, uint32_t type, const Value& value);
};

// the type codes BMessage uses
enum : uint32_t {
	kArchiveInt32 = 0x4c4f4e47,		// 'LONG'
	kArchiveInt16 = 0x53485254,		// 'SHRT'
	kArchiveBool = 0x424f4f4c,		// 'BOOL'
	kArchiveFloat = 0x464c4f54,		// 'FLOT'
	kArchiveString = 0x43535452,	// 'CSTR'
	kArchivePoint = 0x42504e54,		// 'BPNT'
	kArchiveRect = 0x52454354,		// 'RECT'
	kArchiveMessage = 0x4d534747,	// 'MSGG'
	kArchiveRaw = 0x52415754		// 'RAWT'
};

// TStyleManager::ArchiveMarkedStyles: a "Style Definition" message ('STYL')
// per style, as TStyle::Archive writes it ("Style Name", "Style Type",
// "Style Level", "Style Flags", "style_index", "Style Base", "Style Parent",
// "Style Owner", a "Style Property" message ('PROP') per property with its
// "Property Type" and its class's fields). A property of a class the port
// doesn't know the fields of (pens, labels, wraps, gradient inks) is kept
// as its archive bytes in "Property Data", which the original ignores.
ArchiveMessage ArchiveStyles(const std::vector<StyleDefinition>& styles);

// TStyle::Restore of each "Style Definition"; properties the port can't
// read back are left out.
std::vector<StyleDefinition> RestoreStyles(const ArchiveMessage& message);

// The styles file: the header (`header`: a document's first 98 bytes,
// with the identity and the type 'STYL'; zeros past what it has), the version (1), the flattened message, 'STYL'.
std::vector<uint8_t> StylesFileBytes(const std::vector<uint8_t>& header,
	const std::vector<uint8_t>& flattened);

// The flattened message of a styles file. False with `error` if it isn't
// one; `newer` is set if its version is later than 1 (STR# 6, 39).
bool ReadStylesFile(const std::vector<uint8_t>& file, std::vector<uint8_t>& flattened,
	bool& newer, std::string& error);

} // namespace gobe

#endif
