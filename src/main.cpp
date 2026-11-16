#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "BalancedMerge2F/BalancedMerge2F.hpp"
#include "Common.hpp"
#include "FPlus1/MergeFPlus1.hpp"
#include "File.hpp"
#include "QuickSort/QuickSort.hpp"

enum MethodType : uint8_t {
    BalancedMerge2F = 1,
    BalancedMergeFPlus1Method = 2,
    ExternalQuickSort = 3,
};

enum FileType : uint8_t {
    Ascending = 1,
    Descending = 2,
    Unordered = 3,
};

namespace {

bool ParseStrictLong(std::string_view text, long &out) {
    if (text.empty()) {
        return false;
    }
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

std::filesystem::path ResolveInputFilePath(const char *argv0,
                                           FileType situation) {
    std::string fileTypeName;
    switch (situation) {
    case Ascending:
        fileTypeName = "provao_ascending";
        break;
    case Descending:
        fileTypeName = "provao_descending";
        break;
    case Unordered:
        fileTypeName = "provao_unordered";
        break;
    }

    const auto execDir = std::filesystem::path(argv0).parent_path();
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::path("tmp") / fileTypeName,
        execDir / ".." / "tmp" / fileTypeName,
        execDir / "tmp" / fileTypeName,
    };

    // Situação 3 corresponde ao PROVAO.TXT original caso provao_unordered ainda
    // não tenha sido extraído em tmp/.
    if (situation == Unordered) {
        for (const char *originalName : {"PROVAO.TXT", "PROVAO.txt"}) {
            candidates.emplace_back(originalName);
            candidates.push_back(execDir / ".." / originalName);
            candidates.push_back(execDir / originalName);
        }
    }

    for (const auto &candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.front();
}

}  // namespace

int main(int argc, char *argv[]) {
    constexpr int requiredArgC = 4;
    constexpr int maxArgC = 5;
    if (argc < requiredArgC) {
        Log::Error("Missing arguments. Use: ordena <method> <quantity> "
                   "<situation> [-P]");
        return -1;
    }
    if (argc > maxArgC) {
        Log::Error("Too many arguments. Use: ordena <method> <quantity> "
                   "<situation> [-P]");
        return -1;
    }

    if (argc == maxArgC) {
        constexpr int optionalArgPos = 4;
        if (std::string_view(argv[optionalArgPos]) != "-P") {
            Log::Error("Invalid optional argument '" +
                       std::string(argv[optionalArgPos]) +
                       "'. Expected [-P]. Use: ordena <method> <quantity> "
                       "<situation> [-P]");
            return -1;
        }
        Log::enableInfo = true;
    }

    long methodValue = 0;
    if (!ParseStrictLong(argv[1], methodValue) ||
        methodValue < MethodType::BalancedMerge2F ||
        methodValue > MethodType::ExternalQuickSort) {
        Log::Error("<method> must be an integer: 1 -> 2f balanced merge; "
                   "2 -> f+1 balanced merge; 3 -> external quicksort");
        return -1;
    }

    long quantity = 0;
    constexpr long maxFileSize = 471705;
    if (!ParseStrictLong(argv[2], quantity) || quantity <= 0 ||
        quantity > maxFileSize) {
        Log::Error("<quantity> must be an integer greater than 0 and less "
                   "than or equal to 471705");
        return -1;
    }

    long situationValue = 0;
    if (!ParseStrictLong(argv[3], situationValue) ||
        situationValue < FileType::Ascending ||
        situationValue > FileType::Unordered) {
        Log::Error("<situation> must be an integer: 1 -> ascending file; "
                   "2 -> descending file; 3 -> unordered file");
        return -1;
    }

    const std::filesystem::path filePath = ResolveInputFilePath(
        argv[0], static_cast<FileType>(situationValue));

    std::shared_ptr<File> file;
    try {
        file = std::make_shared<File>(filePath.string(),
                                      static_cast<uint64_t>(quantity));
    } catch (const std::exception &e) {
        Log::Error(e.what());
        return -1;
    }

    switch (static_cast<MethodType>(methodValue)) {
    case BalancedMerge2F:
        return RunMethod1(file);
    case BalancedMergeFPlus1Method:
        return RunMethod2(file);
    case ExternalQuickSort:
        return QuickSortExterno::Run(file);
    }

    return -1;
}
