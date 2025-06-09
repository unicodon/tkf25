#include "gif.h"
#include <stdexcept>

#include <iostream>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cassert>

class GifLZWDecoder {
public:
	GifLZWDecoder(const std::vector<uint8_t>& input, int initCodeSize)
		: data(input), initialCodeSize(initCodeSize)
	{
		dataPos = 0;
		bitPos = 0;
	}

	std::vector<uint8_t> decode() {
		std::vector<uint8_t> output;

		initDictionary();

		int codeSize = initialCodeSize + 1;
		int clearCode = 1 << initialCodeSize;
		int endCode = clearCode + 1;
		int nextCode = endCode + 1;
		int prevCode = -1;

		while (true) {
			int code = readCode(codeSize);
			if (code == clearCode) {
				initDictionary();
				codeSize = initialCodeSize + 1;
				nextCode = endCode + 1;
				prevCode = -1;
				continue;
			}
			else if (code == endCode) {
				break;
			}

			std::vector<uint8_t> entry;

			if (code < nextCode && dictionary.count(code)) {
				entry = dictionary[code];
			}
			else if (code == nextCode && prevCode != -1) {
				entry = dictionary[prevCode];
				entry.push_back(entry.front());
			}
			else {
				throw std::runtime_error("Invalid LZW code");
			}

			output.insert(output.end(), entry.begin(), entry.end());

			if (prevCode != -1) {
				std::vector<uint8_t> newEntry = dictionary[prevCode];
				newEntry.push_back(entry.front());
				dictionary[nextCode++] = newEntry;

				if (nextCode == (1 << codeSize) && codeSize < 12) {
					codeSize++;
				}
			}

			prevCode = code;
		}

		return output;
	}

private:
	const std::vector<uint8_t>& data;
	int initialCodeSize;
	size_t dataPos;
	int bitPos;
	std::unordered_map<int, std::vector<uint8_t>> dictionary;

	void initDictionary() {
		dictionary.clear();
		int dictSize = 1 << initialCodeSize;
		for (int i = 0; i < dictSize; ++i) {
			dictionary[i] = { static_cast<uint8_t>(i) };
		}
	}

	int readCode(int codeSize) {
		int rawCode = 0;
		int bitsRead = 0;

		while (bitsRead < codeSize) {
			if (dataPos >= data.size()) throw std::runtime_error("Unexpected end of data");
			uint8_t byte = data[dataPos];

			int availableBits = 8 - bitPos;
			int bitsToRead = std::min(codeSize - bitsRead, availableBits);
			int mask = ((1 << bitsToRead) - 1) << bitPos;

			rawCode |= ((byte & mask) >> bitPos) << bitsRead;

			bitsRead += bitsToRead;
			bitPos += bitsToRead;

			if (bitPos >= 8) {
				bitPos = 0;
				++dataPos;
			}
		}

		return rawCode;
	}
};

// LZWデコード用の関数
std::vector<uint8_t> decodeLZW(const std::vector<uint8_t>& compressedData, uint8_t minCodeSize)
{
	GifLZWDecoder decoder(compressedData, minCodeSize);
	return decoder.decode();
}
