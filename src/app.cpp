#include <unifex/task.hpp>
#include <unifex/when_all.hpp>
#include <unifex/sync_wait.hpp>
#include "mainwq.h"
#include "curl_workqueue.h"
#include "gif.h"

CurlWorkqueue* g_curlWQ;

void SetImage(const std::vector<uint32_t>& image, int width, int height, int id);

unifex::task<void> processImageBlock(CurlWorkqueue::CurlReader& reader, ImageDescriptor& descriptor, std::vector<uint8_t>& imageData, std::vector<uint8_t>& localColorTable)
{
	if (co_await reader.read(&descriptor, sizeof(descriptor)) != sizeof(descriptor)) {
		printf("Failed to read Image Descriptor\n");
		co_return;
	}

	printf("Image Descriptor: %dx%d at (%d, %d)\n",
		descriptor.width, descriptor.height, descriptor.left, descriptor.top);

	// ���[�J���J���[�e�[�u���̏��� (�K�v�Ȃ�)
	if (descriptor.packedFields & 0x80) {
		size_t colorTableSize = 3 * (1 << ((descriptor.packedFields & 0x07) + 1));
		localColorTable.resize(colorTableSize);
		if (co_await reader.read(localColorTable.data(), colorTableSize) != colorTableSize) {
			printf("Failed to read Local Color Table\n");
			co_return;
		}
		printf("Local Color Table read successfully\n");
	}
	else {
		localColorTable.clear(); // ���[�J���J���[�e�[�u�����Ȃ��ꍇ�̓N���A
	}

	// LZW�ŏ��R�[�h�T�C�Y��ǂݎ��
	uint8_t minCodeSize;
	if (co_await reader.read(&minCodeSize, 1) != 1) {
		printf("Failed to read LZW Minimum Code Size\n");
		co_return;
	}
	printf("LZW Minimum Code Size: %d\n", minCodeSize);

	// ���k�f�[�^��ǂݎ��
	std::vector<uint8_t> compressedData;
	while (true) {
		uint8_t blockSize;
		if (co_await reader.read(&blockSize, 1) != 1) {
			printf("Failed to read block size\n");
			co_return;
		}
		if (blockSize == 0) {
			break; // �u���b�N�I��
		}
		std::vector<uint8_t> block(blockSize);
		if (co_await reader.read(block.data(), blockSize) != blockSize) {
			printf("Failed to read block data\n");
			co_return;
		}
		compressedData.insert(compressedData.end(), block.begin(), block.end());
	}

	// LZW�f�R�[�h
	try {
		imageData = decodeLZW(compressedData, minCodeSize);
		printf("Image data decoded successfully, size: %zu\n", imageData.size());
	}
	catch (const std::exception& e) {
		printf("LZW decode error: %s\n", e.what());
	}
}

unifex::task<void> readGraphicsControlExtension(CurlWorkqueue::CurlReader& reader, GraphicControlExtension& gce)
{
	// �O���t�B�b�N����g���u���b�N��ǂݎ��
	printf("Reading Graphic Control Extension Block\n");
	uint8_t blockSize;
	if ((co_await reader.read(&blockSize, 1)) != 1 || blockSize != 4) {
		printf("Invalid Graphic Control Extension Block size\n");
		co_return;
	}
	if ((co_await reader.read(&gce, sizeof(gce))) != sizeof(gce)) {
		printf("Failed to read Graphic Control Extension\n");
		co_return;
	}
	uint8_t terminator;
	if ((co_await reader.read(&terminator, 1)) != 1 || terminator != 0) {
		printf("Invalid Graphic Control Extension terminator\n");
		co_return;
	}
	printf("Graphic Control Extension: delayTime=%d, transparentColorIndex=%d\n",
		gce.delayTime, gce.transparentColorIndex);
}

unifex::task<void> readExtensionBlock(CurlWorkqueue::CurlReader& reader)
{
	// �T�u�u���b�N���X�L�b�v
	while (true) {
		uint8_t subBlockSize;
		if ((co_await reader.read(&subBlockSize, 1)) != 1) {
			printf("Failed to read sub-block size\n");
			co_return;
		}
		if (subBlockSize == 0) {
			break; // �T�u�u���b�N�I��
		}
		std::vector<char> subBlock(subBlockSize);
		if ((co_await reader.read(subBlock.data(), subBlockSize)) != subBlockSize) {
			printf("Failed to skip sub-block data\n");
			co_return;
		}
	}
}

unifex::task<void> handlApplicationExtensionBlock(CurlWorkqueue::CurlReader& reader)
{
	// �A�v���P�[�V�����g���u���b�N
	printf("Application Extension Block found\n");

	// �u���b�N�T�C�Y��ǂݎ��
	uint8_t blockSize;
	if ((co_await reader.read(&blockSize, 1)) != 1 || blockSize != 0x0B) {
		printf("Invalid Application Extension Block size\n");
		co_return;
	}

	// �A�v���P�[�V�������ʎq�ƔF���R�[�h��ǂݎ��
	char appIdentifier[8 + 1] = { 0 };
	char appAuthCode[3 + 1] = { 0 };
	if ((co_await reader.read(appIdentifier, 8)) != 8 ||
		(co_await reader.read(appAuthCode, 3)) != 3) {
		printf("Failed to read Application Extension Block identifiers\n");
		co_return;
	}

	printf("Application Identifier: %s\n", appIdentifier);
	printf("Application Authentication Code: %s\n", appAuthCode);

	// �f�[�^�T�u�u���b�N��ǂݎ��
	std::vector<uint8_t> appData;
	while (true) {
		uint8_t subBlockSize;
		if ((co_await reader.read(&subBlockSize, 1)) != 1) {
			printf("Failed to read sub-block size\n");
			co_return;
		}
		if (subBlockSize == 0) {
			break; // �T�u�u���b�N�I��
		}

		std::vector<uint8_t> subBlock(subBlockSize);
		if ((co_await reader.read(subBlock.data(), subBlockSize)) != subBlockSize) {
			printf("Failed to read sub-block data\n");
			co_return;
		}
		appData.insert(appData.end(), subBlock.begin(), subBlock.end());
	}

	printf("Application Extension Block data size: %zu bytes\n", appData.size());

	// �K�v�ɉ����ăA�v���P�[�V�����f�[�^�����
	// ��: "NETSCAPE2.0" �̏ꍇ�A���[�v�����񂪊܂܂��
	if (std::string(appIdentifier) == "NETSCAPE") {
		printf("NETSCAPE Application Extension detected\n");
		if (appData.size() >= 3 && appData[0] == 0x01) {
			uint16_t loopCount = appData[1] | (appData[2] << 8);
			if (loopCount == 0) {
				printf("Loop Count: Infinite\n");
			}
			else {
				printf("Loop Count: %d\n", loopCount);
			}
		}
		else {
			printf("Invalid NETSCAPE Application Extension data\n");
		}
	}
}

unifex::task<void> curl_task_once(const char* url, int taskIndex)
{
	// �l�b�g���[�N�X���b�h�ɃX�P�W���[��
	co_await shedule(*g_curlWQ);

	auto reader = CurlWorkqueue::CurlReader(url, *g_curlWQ);

	GIFHeader header;
	if ((co_await reader.read(&header, sizeof(header))) != sizeof(header)) {
		printf("Failed to read GIF header\n");
		co_return;
	}

	LogicalScreenDescriptor lsd;
	if ((co_await reader.read(&lsd, sizeof(lsd))) != sizeof(lsd)) {
		printf("Failed to read Logical Screen Descriptor\n");
		co_return;
	}

	std::vector<uint32_t> image(lsd.width * lsd.height);

	// �O���[�o���J���[�e�[�u���̑��݂��m�F
	std::vector<uint8_t> globalColorTable;
	if (lsd.packedFields & 0x80) { // �O���[�o���J���[�e�[�u�������݂��邩�m�F
		size_t colorTableSize = 3 * (1 << ((lsd.packedFields & 0x07) + 1));
		globalColorTable.resize(colorTableSize);
		if ((co_await reader.read(globalColorTable.data(), colorTableSize)) != colorTableSize) {
			printf("Failed to read Global Color Table\n");
			co_return;
		}
	}

	std::optional<GraphicControlExtension> gce;
	while (true) {
		uint8_t blockType;
		if ((co_await reader.read(&blockType, 1)) != 1) {
			printf("Failed to read block type\n");
			co_return;
		}

		if (blockType == 0x3B) { // �I�[�o�C�g
			printf("End of GIF file\n");
			break;
		}
		else if (blockType == 0x2C) { // �摜�u���b�N
			printf("Image block found\n");
			ImageDescriptor descriptor;
			std::vector<uint8_t> imageData;
			std::vector<uint8_t> localColorTable;
			co_await processImageBlock(reader, descriptor, imageData, localColorTable);
			if (!imageData.empty()) {
				printf("Image data size: %zu bytes\n", imageData.size());
				uint8_t* colorTable = localColorTable.empty() ? globalColorTable.data() : localColorTable.data();
				int colorTableSize = localColorTable.empty() ? globalColorTable.size() : localColorTable.size();

				int transparentColorIndex = -1;
				if (gce && (gce->packedFields & 0x1)) {
					transparentColorIndex = gce->transparentColorIndex;
				}
				if (colorTableSize % 3 != 0) {
					co_return;
				}
				for (size_t i = 0; i < imageData.size(); ++i) {
					uint8_t index = imageData[i];
					int imageIndex = (descriptor.left + (i % descriptor.width)) +
						((descriptor.top + (i / descriptor.width)) * lsd.width);
					if (imageIndex >= image.size()) {
						printf("Image index out of bounds: %d\n", imageIndex);
						continue; // �A�E�g�I�u�o�E���Y�͖���
					}
					if (index == transparentColorIndex) {
						continue;
					}
					else if (index < 0 || index >= 256) {
						printf("Invalid color index: %d\n", index);
						image[imageIndex] = 0xFF000000; // �A�E�g�I�u�o�E���Y�͓���
					}
					if (index < colorTableSize) {
						image[imageIndex] = 0xFF000000 |
							(colorTable[index * 3 + 0] << 16) |
							(colorTable[index * 3 + 1] << 8) |
							colorTable[index * 3 + 2];
					}
					else {
						image[imageIndex] = 0xFF000000; // �A�E�g�I�u�o�E���Y�͓���
					}
				}
				if (gce) {
					co_await sheduleOnMainWQ(std::chrono::milliseconds(gce->delayTime * 10));
					gce = std::nullopt;
				}
				else {
					co_await sheduleOnMainWQ();
				}
				SetImage(image, lsd.width, lsd.height, taskIndex);
			}
			else {
				printf("No image data found\n");
			}
		}
		else if (blockType == 0x21) { // �g���u���b�N
			uint8_t label;
			if ((co_await reader.read(&label, 1)) != 1) {
				printf("Failed to read extension label\n");
				co_return;
			}
			if (label == 0xF9) { // �O���t�B�b�N����g��
				GraphicControlExtension gce_;
				co_await readGraphicsControlExtension(reader, gce_);
				gce = gce_;
			}
			else if (label == 0xFE) { // �R�����g�g��
				co_await readExtensionBlock(reader);
			}
			else if (label == 0x01) { // �v���[���e�L�X�g�g��
				co_await readExtensionBlock(reader);
			}
			else if (label == 0xFF) { // �A�v���P�[�V�����g��
				co_await handlApplicationExtensionBlock(reader);
			}
			else {
				co_await readExtensionBlock(reader);
			}
		}
		else if (blockType == 0xFF) { // Application�g���u���b�N
			co_await handlApplicationExtensionBlock(reader);
		}
		else {
			printf("Unknown block type: 0x%02X\n", blockType);
			co_return;
		}
	}
}

unifex::task<void> curl_task(const char* url, int taskIndex)
{
	while (true) {
		co_await curl_task_once(url, taskIndex);
	}
}

unifex::task<void> main_task()
{
	g_curlWQ = new CurlWorkqueue();
	std::thread{ [&]() { g_curlWQ->run(); } }.detach();

	const char* urls[] = {
		"https://upload.wikimedia.org/wikipedia/commons/2/2c/Rotating_earth_%28large%29.gif",
		"https://media3.giphy.com/media/v1.Y2lkPTc5MGI3NjExd3B1YTh4NzdrcXQ1MGd4Ymxld3c5eWk3MnBwdDdwemlrNXQxOXh0YSZlcD12MV9pbnRlcm5hbF9naWZfYnlfaWQmY3Q9Zw/BfbUe877N4xsUhpcPc/giphy.gif",
		"https://media0.giphy.com/media/v1.Y2lkPTc5MGI3NjExbTliYmxnbWJtdDBtODI1djI0MnpydmljMmp5eXdrZWdvYjAzdXU4aiZlcD12MV9pbnRlcm5hbF9naWZfYnlfaWQmY3Q9Zw/AGzrIm03v0zQrIVI52/giphy.gif",
		"https://media4.giphy.com/media/v1.Y2lkPTc5MGI3NjExanN4c3IyYW81OHl0N2VzbG0zcTNkcDdibWJubDFycjBtZWxlcng2NCZlcD12MV9pbnRlcm5hbF9naWZfYnlfaWQmY3Q9Zw/kaVe0g311RVYdGlibZ/giphy.gif",
	};

	unifex::sync_wait(unifex::when_all(
		curl_task(urls[0], 0),
		curl_task(urls[1], 1),
		curl_task(urls[2], 2),
		curl_task(urls[3], 3)
	));
	co_return;
}