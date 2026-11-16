#include "BinaryIO.hpp"

#include <algorithm>
#include <stdexcept>

namespace BinaryIO {

void EnsureParentDirectory(const std::filesystem::path &filePath) {
    const std::filesystem::path parent = filePath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void WriteItemsPage(std::ostream &out,
                    const std::array<Item, PAGE_SIZE> &page,
                    std::size_t count, const std::string &errorMessage) {
    if (count == 0) {
        return;
    }

    out.write(reinterpret_cast<const char *>(page.data()),
              static_cast<std::streamsize>(count * sizeof(Item)));
    if (!out) {
        throw std::runtime_error(errorMessage);
    }
}

uint64_t ReadItemsPage(std::istream &in, std::array<Item, PAGE_SIZE> &page,
                       uint64_t maxItems) {
    const uint64_t toRead = std::min<uint64_t>(PAGE_SIZE, maxItems);
    if (toRead == 0) {
        return 0;
    }

    in.read(reinterpret_cast<char *>(page.data()),
            static_cast<std::streamsize>(toRead * sizeof(Item)));
    const auto bytesRead = static_cast<uint64_t>(in.gcount());
    return bytesRead / sizeof(Item);
}

}  // namespace BinaryIO
