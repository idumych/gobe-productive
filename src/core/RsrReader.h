// Port of Gobe's TRsrHelper: reads the primitives its resource formats are
// built from. Integers are 32-bit little-endian (TRsrHelper's constructor
// sets "no byte swap"), strings are NUL-terminated, bools are u32, rects are
// four int32s. Reads past the end return zeros and set Overrun().
#ifndef GOBE_RSR_READER_H
#define GOBE_RSR_READER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gobe {

class RsrReader {
public:
	RsrReader(const uint8_t* data, size_t size);
	explicit RsrReader(const std::vector<uint8_t>& data);

	bool More() const { return fPos < fSize; }
	bool Overrun() const { return fOverrun; }
	size_t Position() const { return fPos; }

	uint32_t UInt32();
	int32_t Int32() { return int32_t(UInt32()); }
	bool Bool() { return UInt32() != 0; }
	std::string String();
	void Rect(float rect[4]);

private:
	const uint8_t* fData;
	size_t fSize;
	size_t fPos = 0;
	bool fOverrun = false;
};

} // namespace gobe

#endif
