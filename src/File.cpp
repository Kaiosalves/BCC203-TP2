#include "File.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "Common.hpp"
#include "Item.hpp"

File::File(const std::string &path, uint64_t quantity)
    : path_(path), quantity_(quantity) {
    this->file_ = std::ifstream(path);

    if (!this->file_.is_open()) {
        throw std::runtime_error("Failed to open file");
    }
}

File::~File() { this->file_.close(); }

std::array<Item, PAGE_SIZE> File::GetNextPage() {
    std::array<Item, PAGE_SIZE> page{};
    this->lastPageCount_ = 0;

    if (this->eof()) {
        Log::Error("Invalid input file access index");
        return page;
    }

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

    uint64_t counter = 0;
    std::string line;
    while (counter < PAGE_SIZE && this->itemsRead_ < this->quantity_ &&
           std::getline(this->file_, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }

        if (line.size() < LINE_LEN) {
            line.resize(LINE_LEN, ' ');
        }

        Item reg = Item();
        reg.registrationNumber = std::stol(line.substr(REG_POS, REG_LEN));
        reg.grade = std::stof(line.substr(GRADE_POS, GRADE_LEN));
        line.copy(reg.state.data(), STATE_LEN, STATE_POS);
        line.copy(reg.city.data(), CITY_LEN, CITY_POS);
        line.copy(reg.degree.data(), DEGREE_LEN, DEGREE_POS);

        page[counter] = reg;

        counter++;
        this->itemsRead_++;
    }

    if (counter > 0) {
        Metrics::RecordDiskRead();
    }

    this->lastPageCount_ = counter;
    return page;
}

void File::Rewind() {
    this->file_.clear();
    this->file_.seekg(0, std::ios::beg);
    this->itemsRead_ = 0;
    this->lastPageCount_ = 0;
}

std::string File::path() const { return this->path_; }

std::filesystem::file_time_type File::lastModification() const {
    return std::filesystem::last_write_time(this->path_);
}

uint64_t File::size() const { return std::filesystem::file_size(this->path_); }

uint64_t File::quantity() const { return this->quantity_; }

uint64_t File::lastPageCount() const { return this->lastPageCount_; }

uint64_t File::itemsRead() const { return this->itemsRead_; }

bool File::eof() {
    return this->itemsRead_ >= this->quantity_ || this->file_.eof();
}
