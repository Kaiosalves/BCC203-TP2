#include "File.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "BinaryIO.hpp"
#include "Common.hpp"
#include "FileConverter.hpp"
#include "RecordFormat.hpp"

namespace {

class MetricsPauseGuard {
  public:
    explicit MetricsPauseGuard(File &file, bool previous)
        : file_(file), previous_(previous) {}
    ~MetricsPauseGuard() {
        file_.Rewind();
        file_.SetRecordMetrics(previous_);
    }

    MetricsPauseGuard(const MetricsPauseGuard &) = delete;
    MetricsPauseGuard &operator=(const MetricsPauseGuard &) = delete;

  private:
    File &file_;
    bool previous_;
};

}  // namespace

File::File(const std::string &path, uint64_t quantity,
           const std::string &outputPath)
    : path_(path),
      binaryPath_(FileConverter::BuildIntermediateBinaryPath(path)),
      outputPath_(outputPath.empty()
                      ? FileConverter::BuildDefaultOutputPath(path)
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

Item File::ParseLine(std::string line) {
    return RecordFormat::ParseLine(std::move(line));
}

std::string File::FormatProvaoLine(const Item &item) {
    return RecordFormat::FormatProvaoLine(item);
}

std::string File::FormatDisplayLine(const Item &item) {
    return RecordFormat::FormatDisplayLine(item);
}

void File::ConvertTextToBinary() {
    this->CloseStream();
    FileConverter::ConvertTextToBinary(this->path_, this->binaryPath_,
                                       this->quantity_);
}

std::string File::ConvertBinaryToText(const std::string &outputPath) const {
    const std::string targetPath =
        outputPath.empty() ? this->outputPath_ : outputPath;
    FileConverter::ConvertBinaryToText(this->binaryPath_, targetPath,
                                       this->quantity_);
    return targetPath;
}

std::array<Item, PAGE_SIZE> File::GetNextPage() {
    std::array<Item, PAGE_SIZE> page{};
    this->lastPageCount_ = 0;

    if (this->eof()) {
        Log::Error("Invalid input file access index");
        return page;
    }

    const uint64_t itemsActuallyRead = BinaryIO::ReadItemsPage(
        this->file_, page, this->quantity_ - this->itemsRead_);

    if (itemsActuallyRead > 0 && this->recordMetrics_) {
        Metrics::RecordDiskRead();
    }

    this->lastPageCount_ = itemsActuallyRead;
    this->itemsRead_ += itemsActuallyRead;
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

template <typename Fn> void File::ForEachPage(Fn &&fn) {
    const bool previousMetrics = this->recordMetrics_;
    this->recordMetrics_ = false;
    this->Rewind();
    const MetricsPauseGuard guard(*this, previousMetrics);

    while (!this->eof()) {
        const auto page = this->GetNextPage();
        const uint64_t count = this->lastPageCount_;
        if (count == 0) {
            break;
        }
        fn(page, count);
    }
}

void File::PrintContents() {
    std::cout << "--- Dados a serem ordenados (" << this->quantity_
              << " registros) ---\n";
    this->ForEachPage([](const auto &page, uint64_t count) {
        for (uint64_t i = 0; i < count; ++i) {
            std::cout << FormatDisplayLine(page[i]) << '\n';
        }
    });
    std::cout << std::flush;
}

bool File::VerifySorted(bool print) {
    if (print) {
        std::cout << "--- Resultado da ordenação ---\n";
    }

    Item previous{};
    uint64_t count = 0;
    bool ordered = true;

    this->ForEachPage([&](const auto &page, uint64_t pageCount) {
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
    });

    if (print) {
        std::cout << std::flush;
    }

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
