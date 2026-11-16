#include "FileConverter.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "BinaryIO.hpp"
#include "Common.hpp"
#include "Item.hpp"
#include "RecordFormat.hpp"

namespace {

std::filesystem::path ResolveParentDir(const std::filesystem::path &input) {
    return input.has_parent_path() ? input.parent_path()
                                   : std::filesystem::path(".");
}

}  // namespace

namespace FileConverter {

std::string BuildIntermediateBinaryPath(const std::string &inputPath) {
    static std::atomic<uint64_t> counter{0};
    const std::filesystem::path input(inputPath);
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const uint64_t seq = counter.fetch_add(1, std::memory_order_relaxed);
    const std::string filename = input.stem().string() + "_intermediario_" +
                                 std::to_string(stamp) + "_" +
                                 std::to_string(seq) + ".bin";
    return (ResolveParentDir(input) / filename).string();
}

std::string BuildDefaultOutputPath(const std::string &inputPath) {
    const std::filesystem::path input(inputPath);
    return (ResolveParentDir(input) / (input.stem().string() + "_ordenado.txt"))
        .string();
}

void ConvertTextToBinary(const std::string &textPath,
                         const std::string &binaryPath, uint64_t quantity) {
    std::ifstream in(textPath);
    if (!in.is_open()) {
        throw std::runtime_error("Falha ao abrir arquivo texto de entrada: " +
                                 textPath);
    }

    BinaryIO::EnsureParentDirectory(binaryPath);

    std::ofstream out(binaryPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Falha ao criar arquivo binário intermediário: " + binaryPath);
    }

    const std::string writeError =
        "Falha ao gravar no arquivo binário intermediário: " + binaryPath;

    std::array<Item, PAGE_SIZE> page{};
    uint64_t pageCount = 0;
    uint64_t converted = 0;
    std::string line;

    while (converted < quantity && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }

        page[pageCount++] = RecordFormat::ParseLine(std::move(line));
        ++converted;

        if (pageCount == PAGE_SIZE) {
            BinaryIO::WriteItemsPage(out, page, pageCount, writeError);
            pageCount = 0;
        }
    }

    BinaryIO::WriteItemsPage(out, page, pageCount, writeError);

    out.flush();
    if (!out) {
        throw std::runtime_error(
            "Falha ao finalizar gravação do arquivo binário intermediário: " +
            binaryPath);
    }

    if (converted < quantity) {
        throw std::runtime_error(
            "O arquivo de entrada possui apenas " + std::to_string(converted) +
            " registros, mas foram pedidos " + std::to_string(quantity));
    }

    Log::Info("Arquivo texto de entrada convertido para binário intermediário: " +
              binaryPath);
}

void ConvertBinaryToText(const std::string &binaryPath,
                         const std::string &textPath, uint64_t quantity) {
    std::ifstream in(binaryPath, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(
            "Falha ao abrir arquivo binário intermediário para conversão: " +
            binaryPath);
    }

    BinaryIO::EnsureParentDirectory(textPath);

    std::ofstream out(textPath, std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Falha ao criar arquivo texto final ordenado: " + textPath);
    }

    std::array<Item, PAGE_SIZE> page{};
    uint64_t converted = 0;

    while (converted < quantity) {
        const uint64_t readCount =
            BinaryIO::ReadItemsPage(in, page, quantity - converted);
        if (readCount == 0) {
            break;
        }

        for (uint64_t i = 0; i < readCount; ++i) {
            out << RecordFormat::FormatProvaoLine(page[i]) << '\n';
        }
        if (!out) {
            throw std::runtime_error(
                "Falha ao gravar arquivo texto final ordenado: " + textPath);
        }

        converted += readCount;
    }

    out.flush();
    if (!out) {
        throw std::runtime_error(
            "Falha ao finalizar arquivo texto final ordenado: " + textPath);
    }

    if (converted < quantity) {
        throw std::runtime_error(
            "O arquivo binário intermediário possui apenas " +
            std::to_string(converted) + " registros, mas eram esperados " +
            std::to_string(quantity));
    }
}

}  // namespace FileConverter
