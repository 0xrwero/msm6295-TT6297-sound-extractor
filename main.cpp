#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <Windows.h>
#include <string>

constexpr std::uint32_t TT6297_ADPCM_SPEECH_DATA_SECTION_MAX_SIZE = 16 * 1024 * 1024; // in bytes
constexpr std::uint32_t MSM6295_ADPCM_SPEECH_DATA_SECTION_MAX_SIZE = 0.25 * 1024 * 1024; // in bytes

constexpr std::uint8_t TT6297_ADDRESS_DATA_SECTION_START = 0x8; // First start address memory position for the external rom interfaced by the TT6297
constexpr std::uint16_t TT6297_ADDRESS_DATA_SECTION_END = 0xFFF;

constexpr std::uint8_t MSM6295_ADDRESS_DATA_SECTION_START = 0x8;
constexpr std::uint16_t MSM6295_ADDRESS_DATA_SECTION_END = 0x3FF;

enum ChipType
{
	TT6297 = 1,
	MSM6295 = 2
};

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "MSM6295/TT62967 External ROM Sound Extractor (0xRWERO software)" << std::endl;
		std::cout << "arg1: <sound rom type: (msm6295 (1), tt6297 (2)" << std::endl;
		std::cout << "arg2: <input: path to sound binary>" << std::endl;
		std::cout << "arg3: <output: output folder>" << std::endl;

		return 0;
	}

	std::uint32_t chip_type = std::atoi(argv[1]);

	// Open the specified file in binary mode.
	std::ifstream file(argv[2], std::ifstream::binary);

	std::string output_folder = argv[3];

	if (!file.is_open())
	{
		std::cout << "Failed to open the specified input binary file." << std::endl;

		return -1;
	}

	// Calculate the size of the specified file.
	const auto file_size = std::filesystem::file_size(argv[2]);

	if (file_size < TT6297_ADDRESS_DATA_SECTION_END && file_size < MSM6295_ADDRESS_DATA_SECTION_END)
	{
		std::cout << "Input binary file does not correspond to external rom expected of TT6297 or MSM6295. Address data section not identified" << std::endl;

		return -1;
	}
	else if (file_size > (TT6297_ADDRESS_DATA_SECTION_END + TT6297_ADPCM_SPEECH_DATA_SECTION_MAX_SIZE) && (MSM6295_ADDRESS_DATA_SECTION_END + MSM6295_ADPCM_SPEECH_DATA_SECTION_MAX_SIZE))
	{
		std::cout << "Input binary file does not correspond to external rom expected of TT6297 or MSM6295. Specified input file size over max capacity." << std::endl;

		return -1;
	}

	// Map the specified input file to memory to prevent further syscalls.

	const auto file_buffer = std::make_unique<std::uint8_t[]>(file_size);

	file.read(reinterpret_cast<char*>(file_buffer.get()), file_size);

	// Close the specified file stream early, as it is no-longer needed.

	file.close();

	// Read the first 8 bytes of the specified input file buffer. If something is written in those first eight bytes, then an address data section does not exist.

	std::uint64_t u64 = *reinterpret_cast<std::uint64_t*>(file_buffer.get());

	if (u64)
	{
		std::cout << "Input binary file does not correspond to external rom expected of TT6297 or MSM6295. Address data section not identified" << std::endl;

		return -1;
	}

	// Perform one final address data section verification.

	std::uint16_t p = sizeof(std::uint64_t);

	while (p < ((chip_type == ChipType::MSM6295) ? MSM6295_ADDRESS_DATA_SECTION_END : TT6297_ADDRESS_DATA_SECTION_END))
	{
		std::uint16_t addresses = *reinterpret_cast<std::uint16_t*>((file_buffer.get() + p) + sizeof(std::uint32_t) + sizeof(std::uint16_t));

		// The last two bytes should be empty.

		if (addresses)
		{
			std::cout << "Input binary file does not correspond to external rom expected of TT6297 or MSM6295. Address data section not identified" << std::endl;
			
			return -1;
		}

		p += sizeof(std::uint64_t);
	}

	std::cout << "Input file is valid. Address data section identified. Isolating ADPCM byte samples" << std::endl;

	std::filesystem::create_directory(argv[3]);

	p = sizeof(std::uint64_t);

	std::uint16_t index = 1;

	while (p < ((chip_type == ChipType::MSM6295) ? MSM6295_ADDRESS_DATA_SECTION_END : TT6297_ADDRESS_DATA_SECTION_END))
	{
		// Big endian address format is expected of the external ROM, so we need to extract each individual byte.
		std::uint64_t addresses = *reinterpret_cast<std::uint64_t*>(file_buffer.get() + p);

		if (!addresses)
		{
			break;
		}

		std::array<std::uint8_t, 6> sa_ea_adrs{};

		sa_ea_adrs[0] = (addresses) >> 16;
		sa_ea_adrs[1] = (addresses) >> 8;
		sa_ea_adrs[2] = (addresses);

		sa_ea_adrs[3] = (addresses) >> 40;
		sa_ea_adrs[4] = (addresses) >> 32;
		sa_ea_adrs[5] = (addresses) >> 24;

		// Extract the start and end address from the eight bytes of memory that we have read. Perform bitmask as we only want three bytes, not four.
		std::uint32_t start_address = *reinterpret_cast<std::uint32_t*>(sa_ea_adrs.data()) & 0xFFFFFF;
		std::uint32_t end_address = *reinterpret_cast<std::uint32_t*>(sa_ea_adrs.data() + sizeof(std::uint16_t) + sizeof(std::uint8_t)) & 0xFFFFFF;

		std::uint32_t address_difference = (end_address - start_address);

		if (address_difference > TT6297_ADPCM_SPEECH_DATA_SECTION_MAX_SIZE && address_difference > MSM6295_ADPCM_SPEECH_DATA_SECTION_MAX_SIZE)
		{
			std::cout << "Address leads to out of bounds memory. File is corrupted or not part of the MSM6295 or TT6297. Aborting process." << std::endl;

			return -1;
		}

		std::cout << "Sound identified: (" << address_difference << " bytes) " << "writing to output folder" << std::endl;

		auto adpcm_buffer = std::make_unique<std::uint8_t[]>(address_difference);

		std::memcpy(adpcm_buffer.get(), reinterpret_cast<std::uint32_t*>(file_buffer.get() + start_address), address_difference);

		std::ofstream file(output_folder + "\\" + "sound" + std::to_string(index) + ".bin", std::ofstream::binary);

		file.write(reinterpret_cast<const char*>(adpcm_buffer.get()), (address_difference) - sizeof(std::uint8_t));

		file.close();
		
		index++;

		p += sizeof(std::uint64_t);
	}

	std::cout << "Successfully completed. " << "Total of " << index << " sounds extracted" << std::endl;

}
