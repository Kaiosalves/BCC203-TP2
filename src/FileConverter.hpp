#pragma once

#include <cstdint>
#include <string>

namespace FileConverter {

std::string BuildIntermediateBinaryPath(const std::string &inputPath);
std::string BuildDefaultOutputPath(const std::string &inputPath);

void ConvertTextToBinary(const std::string &textPath,
                         const std::string &binaryPath, uint64_t quantity);

void ConvertBinaryToText(const std::string &binaryPath,
                         const std::string &textPath, uint64_t quantity);

}  // namespace FileConverter
