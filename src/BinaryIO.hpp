#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <ostream>
#include <string>

#include "Common.hpp"
#include "Item.hpp"

namespace BinaryIO {

void EnsureParentDirectory(const std::filesystem::path &filePath);

void WriteItemsPage(std::ostream &out,
                    const std::array<Item, PAGE_SIZE> &page,
                    std::size_t count, const std::string &errorMessage);

uint64_t ReadItemsPage(std::istream &in, std::array<Item, PAGE_SIZE> &page,
                       uint64_t maxItems);

}  // namespace BinaryIO
