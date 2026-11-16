#include "File.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Common.hpp"
#include "Item.hpp"

namespace {

std::string TrimRight(const char *data, std::size_t size) {
    std::string text(data, size);
    const auto last = text.find_last_not_of(" \0", std::string::npos, 2);
    text.erase(last == std::string::npos ? 0 : last + 1);
    return text;
}

}  // namespace

File::File(const std::string &path, uint64_t quantity,
           const std::string &outputPath)
    : path_(path), binaryPath_(BuildIntermediateBinaryPath(path)),
      outputPath_(outputPath.empty() ? BuildDefaultOutputPath(path)
                                     : outputPath),
      quantity_(quantity) {
    if (quantity == 0) {
        throw std::invalid_argument(
            "A quantidade de registros deve ser maior que zero");
    }

    try {
        this->ConvertTextToBinary();
        this->ReopenStream();
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(this->binaryPath_, ec);
        throw;
    }
}

File::~File() {
    this->CloseStream();

    std::error_code ec;
    std::filesystem::remove(this->binaryPath_, ec);
}

std::string File::BuildIntermediateBinaryPath(const std::string &inputPath) {
    static std::atomic<uint64_t> counter{0};
    const std::filesystem::path input(inputPath);
    const std::filesystem::path dir =
        input.has_parent_path() ? input.parent_path() : std::filesystem::path(".");
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const uint64_t seq = counter.fetch_add(1, std::memory_order_relaxed);
    const std::string filename = input.stem().string() + "_intermediario_" +
                                 std::to_string(stamp) + "_" +
                                 std::to_string(seq) + ".bin";
    return (dir / filename).string();
}

std::string File::BuildDefaultOutputPath(const std::string &inputPath) {
    const std::filesystem::path input(inputPath);
    const std::filesystem::path dir =
        input.has_parent_path() ? input.parent_path() : std::filesystem::path(".");
    return (dir / (input.stem().string() + "_ordenado.txt")).string();
}

Item File::ParseLine(std::string line) {
    constexpr size_t REG_POS = 0;
    constexpr size_t REG_LEN = 8;
    constexpr size_t GRADE_POS = 9;
    constexpr size_t GRADE_LEN = 5;
    constexpr size_t STATE_POS = 15;
    constexpr size_t STATE_LEN = 2;
    constexpr size_t CITY_POS = 18;
    constexpr size_t CITY_LEN = 50;
    constexpr size_t DEGREE_POS = 69;
    constexpr size_t DEGREE_LEN = 30;
    constexpr size_t LINE_LEN = DEGREE_POS + DEGREE_LEN;

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.size() < LINE_LEN) {
        line.resize(LINE_LEN, ' ');
    }

    Item reg{};
    reg.registrationNumber = std::stol(line.substr(REG_POS, REG_LEN));
    reg.grade = std::stof(line.substr(GRADE_POS, GRADE_LEN));
    line.copy(reg.state.data(), STATE_LEN, STATE_POS);
    line.copy(reg.city.data(), CITY_LEN, CITY_POS);
    line.copy(reg.degree.data(), DEGREE_LEN, DEGREE_POS);
    return reg;
}

std::string File::FormatProvaoLine(const Item &item) {
    std::string line;
    line.reserve(99);

    char head[32];
    std::snprintf(head, sizeof(head), "%08ld %5.1f ", item.registrationNumber,
                  static_cast<double>(item.grade));
    line.append(head);

    auto appendFixed = [&line](const char *data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            line.push_back(data[i] == '\0' ? ' ' : data[i]);
        }
    };

    appendFixed(item.state.data(), item.state.size());
    line.push_back(' ');
    appendFixed(item.city.data(), item.city.size());
    line.push_back(' ');
    appendFixed(item.degree.data(), item.degree.size());

    return line;
}

std::string File::FormatDisplayLine(const Item &item) {
    char head[64];
    std::snprintf(head, sizeof(head), "%08ld %5.1f %.2s ",
                  item.registrationNumber, static_cast<double>(item.grade),
                  item.state.data());
    return std::string(head) + TrimRight(item.city.data(), item.city.size()) +
           " | " + TrimRight(item.degree.data(), item.degree.size());
}

void File::ConvertTextToBinary() {
    this->CloseStream();

    std::ifstream in(this->path_);
    if (!in.is_open()) {
        throw std::runtime_error("Falha ao abrir arquivo texto de entrada: " +
                                 this->path_);
    }

    const std::filesystem::path binDir =
        std::filesystem::path(this->binaryPath_).parent_path();
    if (!binDir.empty()) {
        std::filesystem::create_directories(binDir);
    }

    std::ofstream out(this->binaryPath_, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Falha ao criar arquivo binário intermediário: " +
            this->binaryPath_);
    }

    std::array<Item, PAGE_SIZE> page{};
    uint64_t pageCount = 0;
    uint64_t converted = 0;
    std::string line;

    while (converted < this->quantity_ && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }

        page[pageCount++] = ParseLine(std::move(line));
        converted++;

        if (pageCount == PAGE_SIZE) {
            out.write(reinterpret_cast<const char *>(page.data()),
                      static_cast<std::streamsize>(pageCount * sizeof(Item)));
            if (!out) {
                throw std::runtime_error(
                    "Falha ao gravar no arquivo binário intermediário: " +
                    this->binaryPath_);
            }
            pageCount = 0;
        }
    }

    if (pageCount > 0) {
        out.write(reinterpret_cast<const char *>(page.data()),
                  static_cast<std::streamsize>(pageCount * sizeof(Item)));
        if (!out) {
            throw std::runtime_error(
                "Falha ao gravar no arquivo binário intermediário: " +
                this->binaryPath_);
        }
    }

    out.flush();
    if (!out) {
        throw std::runtime_error(
            "Falha ao finalizar gravação do arquivo binário intermediário: " +
            this->binaryPath_);
    }

    if (converted < this->quantity_) {
        throw std::runtime_error(
            "O arquivo de entrada possui apenas " + std::to_string(converted) +
            " registros, mas foram pedidos " +
            std::to_string(this->quantity_));
    }

    Log::Info("Arquivo texto de entrada convertido para binário intermediário: " +
              this->binaryPath_);
}

std::string File::ConvertBinaryToText(const std::string &outputPath) const {
    const std::string targetPath =
        outputPath.empty() ? this->outputPath_ : outputPath;

    std::ifstream in(this->binaryPath_, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error(
            "Falha ao abrir arquivo binário intermediário para conversão: " +
            this->binaryPath_);
    }

    const std::filesystem::path outDir =
        std::filesystem::path(targetPath).parent_path();
    if (!outDir.empty()) {
        std::filesystem::create_directories(outDir);
    }

    std::ofstream out(targetPath, std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Falha ao criar arquivo texto final ordenado: " + targetPath);
    }

    std::array<Item, PAGE_SIZE> page{};
    uint64_t converted = 0;

    while (converted < this->quantity_) {
        const uint64_t remaining = this->quantity_ - converted;
        const uint64_t toRead =
            std::min<uint64_t>(PAGE_SIZE, remaining);

        in.read(reinterpret_cast<char *>(page.data()),
                static_cast<std::streamsize>(toRead * sizeof(Item)));
        const auto bytesRead = static_cast<uint64_t>(in.gcount());
        const uint64_t itemsActuallyRead = bytesRead / sizeof(Item);
        if (itemsActuallyRead == 0) {
            break;
        }

        for (uint64_t i = 0; i < itemsActuallyRead; ++i) {
            out << FormatProvaoLine(page[i]) << '\n';
        }
        if (!out) {
            throw std::runtime_error(
                "Falha ao gravar arquivo texto final ordenado: " + targetPath);
        }

        converted += itemsActuallyRead;
    }

    out.flush();
    if (!out) {
        throw std::runtime_error(
            "Falha ao finalizar arquivo texto final ordenado: " + targetPath);
    }

    if (converted < this->quantity_) {
        throw std::runtime_error(
            "O arquivo binário intermediário possui apenas " +
            std::to_string(converted) + " registros, mas eram esperados " +
            std::to_string(this->quantity_));
    }

    return targetPath;
}

std::array<Item, PAGE_SIZE> File::GetNextPage() {
    std::array<Item, PAGE_SIZE> page{};
    this->lastPageCount_ = 0;

    if (this->eof()) {
        Log::Error("Invalid input file access index");
        return page;
    }

    const uint64_t remaining = this->quantity_ - this->itemsRead_;
    const uint64_t toRead = std::min<uint64_t>(PAGE_SIZE, remaining);

    if (toRead > 0) {
        this->file_.read(reinterpret_cast<char *>(page.data()),
                         static_cast<std::streamsize>(toRead * sizeof(Item)));
        const auto bytesRead = static_cast<uint64_t>(this->file_.gcount());
        const uint64_t itemsActuallyRead = bytesRead / sizeof(Item);

        if (itemsActuallyRead > 0 && this->recordMetrics_) {
            Metrics::RecordDiskRead();
        }

        this->lastPageCount_ = itemsActuallyRead;
        this->itemsRead_ += itemsActuallyRead;
    }

    return page;
}

bool File::GetNextItem(Item &out) {
    if (this->eof()) {
        return false;
    }

    if (!this->file_.read(reinterpret_cast<char *>(&out), sizeof(Item))) {
        return false;
    }

    this->itemsRead_++;
    return true;
}

void File::Rewind() {
    if (!this->file_.is_open()) {
        this->ReopenStream();
        return;
    }
    this->file_.clear();
    this->file_.seekg(0, std::ios::beg);
    this->itemsRead_ = 0;
    this->lastPageCount_ = 0;
}

void File::CloseStream() {
    if (this->file_.is_open()) {
        this->file_.close();
    }
}

void File::ReopenStream() {
    this->CloseStream();
    this->file_.clear();
    this->file_.open(this->binaryPath_, std::ios::binary);
    if (!this->file_.is_open()) {
        throw std::runtime_error(
            "Falha ao abrir arquivo binário intermediário: " +
            this->binaryPath_);
    }
    this->itemsRead_ = 0;
    this->lastPageCount_ = 0;
}

void File::SetRecordMetrics(bool enabled) { this->recordMetrics_ = enabled; }

void File::PrintContents() {
    const bool previousMetrics = this->recordMetrics_;
    this->recordMetrics_ = false;
    this->Rewind();

    std::cout << "--- Dados a serem ordenados (" << this->quantity_
              << " registros) ---\n";
    while (!this->eof()) {
        const auto page = this->GetNextPage();
        const uint64_t count = this->lastPageCount_;
        if (count == 0) {
            break;
        }
        for (uint64_t i = 0; i < count; ++i) {
            std::cout << FormatDisplayLine(page[i]) << '\n';
        }
    }
    std::cout << std::flush;

    this->Rewind();
    this->recordMetrics_ = previousMetrics;
}

bool File::VerifySorted(bool print) {
    const bool previousMetrics = this->recordMetrics_;
    this->recordMetrics_ = false;
    this->Rewind();

    if (print) {
        std::cout << "--- Resultado da ordenação ---\n";
    }

    Item previous{};
    uint64_t count = 0;
    bool ordered = true;

    while (!this->eof()) {
        const auto page = this->GetNextPage();
        const uint64_t pageCount = this->lastPageCount_;
        if (pageCount == 0) {
            break;
        }
        for (uint64_t i = 0; i < pageCount; ++i) {
            if (count > 0 && page[i].grade < previous.grade) {
                ordered = false;
            }
            if (print) {
                std::cout << FormatDisplayLine(page[i]) << '\n';
            }
            previous = page[i];
            ++count;
        }
    }

    if (print) {
        std::cout << std::flush;
    }

    this->Rewind();
    this->recordMetrics_ = previousMetrics;
    return ordered && count == this->quantity_;
}

std::string File::path() const { return this->path_; }

std::string File::binaryPath() const { return this->binaryPath_; }

std::string File::outputPath() const { return this->outputPath_; }

std::filesystem::file_time_type File::lastModification() const {
    return std::filesystem::last_write_time(this->path_);
}

uint64_t File::size() const {
    return std::filesystem::file_size(this->binaryPath_);
}

uint64_t File::quantity() const { return this->quantity_; }

uint64_t File::lastPageCount() const { return this->lastPageCount_; }

uint64_t File::itemsRead() const { return this->itemsRead_; }

bool File::eof() {
    return !this->file_.is_open() || this->itemsRead_ >= this->quantity_ ||
           this->file_.eof();
}
