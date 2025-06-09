#pragma once

#include <stdint.h>
#include <vector>

#ifdef _MSC_VER
__pragma(pack(push, 1))
#else
#pragma pack(push, 1)
#endif

struct GIFHeader {
	char signature[3]; // "GIF"
	char version[3];   // "87a" or "89a"
};
static_assert(sizeof(GIFHeader) == 6);

struct LogicalScreenDescriptor {
	uint16_t width;
	uint16_t height;
	uint8_t packedFields;
	uint8_t backgroundColorIndex;
	uint8_t pixelAspectRatio;
};
static_assert(sizeof(LogicalScreenDescriptor) == 7);

struct GraphicControlExtension {
	uint8_t packedFields;
	uint16_t delayTime;
	uint8_t transparentColorIndex;
};
static_assert(sizeof(GraphicControlExtension) == 4);

struct ImageDescriptor {
	uint16_t left;
	uint16_t top;
	uint16_t width;
	uint16_t height;
	uint8_t packedFields;
};
static_assert(sizeof(ImageDescriptor) == 9);


#ifdef _MSC_VER
__pragma(pack(pop))
#else
#pragma pack(pop)
#endif


std::vector<uint8_t> decodeLZW(const std::vector<uint8_t>& compressedData, uint8_t minCodeSize);
