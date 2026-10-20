#include "RsrReader.h"

namespace gobe {

RsrReader::RsrReader(const uint8_t* data, size_t size)
	:
	fData(data),
	fSize(size)
{
}


RsrReader::RsrReader(const std::vector<uint8_t>& data)
	:
	fData(data.data()),
	fSize(data.size())
{
}


uint32_t
RsrReader::UInt32()
{
	if (fPos + 4 > fSize) {
		fOverrun = true;
		fPos = fSize;
		return 0;
	}
	const uint8_t* p = fData + fPos;
	fPos += 4;
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16
		| uint32_t(p[3]) << 24;
}


std::string
RsrReader::String()
{
	size_t start = fPos;
	while (fPos < fSize && fData[fPos] != 0)
		fPos++;
	std::string s(reinterpret_cast<const char*>(fData + start), fPos - start);
	if (fPos < fSize)
		fPos++;	// the NUL
	else
		fOverrun = true;
	return s;
}


void
RsrReader::Rect(float rect[4])
{
	// GetRect reads integers and converts them to floats
	for (int i = 0; i < 4; i++)
		rect[i] = float(Int32());
}

} // namespace gobe
