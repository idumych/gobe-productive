// Read-only access to the BeOS resources appended to an x86 ELF file (the
// original Gobe Productive binaries). Portable: no Haiku API, so the core can
// be tested on any OS. Format: Haiku headers/private/storage/ResourcesDefs.h;
// see tools/rsrc.py for the same reader in Python.
#ifndef GOBE_RESOURCE_FILE_H
#define GOBE_RESOURCE_FILE_H

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gobe {

class ResourceFile {
public:
	struct Resource {
		uint32_t type;
		int32_t id;
		std::string name;
		std::vector<uint8_t> data;
	};

	// Loads all resources of the file; false (and Error()) on failure.
	bool Load(const std::string& path);
	const std::string& Error() const { return fError; }

	// nullptr if there is no such resource.
	const Resource* Find(uint32_t type, int32_t id) const;
	std::vector<const Resource*> All(uint32_t type) const;

private:
	std::vector<Resource> fResources;
	std::map<std::pair<uint32_t, int32_t>, size_t> fIndex;
	std::string fError;
};

// 'MENU' etc. as a multi-character constant value.
constexpr uint32_t FourCC(const char (&s)[5])
{
	return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16)
		| (uint32_t(uint8_t(s[2])) << 8) | uint32_t(uint8_t(s[3]));
}

std::string FourCCString(uint32_t code);

// GetIndString / GetIndexString: string `index` (from 0) of the 'STR#'
// resource `id` (NUL-terminated strings back to back); "" if there is none.
std::string IndexString(const ResourceFile& file, int32_t id, int index);

} // namespace gobe

#endif
