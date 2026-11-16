#include "Tape.hpp"
#include "Common.hpp"
#include <cstdint>
#include <filesystem>
#include <format>
#include <stdexcept>

uint32_t Tape::lastId_ = 0;

Tape::Tape() {
    this->id_ = Tape::lastId_++;

    std::filesystem::create_directories("./tmp");

    this->file_ =
        std::fstream(this->GetPath(), std::ios::in | std::ios::out |
                                          std::ios::trunc | std::ios::binary);

    if (!this->file_.is_open()) {
        throw std::runtime_error("Failed to open tape file");
    }
}

Tape::~Tape() {
    this->file_.close();

    std::error_code ec;
    std::filesystem::remove(this->GetPath(), ec);
}

// TODO: Add checks for uncompleted blocks
std::array<Item, PAGE_SIZE> Tape::ReadBlock() {
    this->file_.clear();
    this->file_.seekg(static_cast<std::streamoff>(
        sizeof(std::array<Item, PAGE_SIZE>) * this->pos_));

    std::array<Item, PAGE_SIZE> block{};

    this->file_.read(reinterpret_cast<char *>(block.data()),
                     sizeof(std::array<Item, PAGE_SIZE>));
    this->AdvanceBlock();
    if (this->recordMetrics_) {
        Metrics::RecordDiskRead();
    }

    return block;
}

void Tape::WriteBlock(std::array<Item, PAGE_SIZE> block) {
    this->file_.clear();
    this->file_.seekp(static_cast<std::streamoff>(
        sizeof(std::array<Item, PAGE_SIZE>) * this->pos_));

    this->file_.write(reinterpret_cast<const char *>(block.data()),
                      sizeof(std::array<Item, PAGE_SIZE>));
    this->AdvanceBlock();
    if (this->recordMetrics_) {
        Metrics::RecordDiskWrite();
    }
}

void Tape::AdvanceBlock() { this->pos_++; }

void Tape::RetreatBlock() {
    if (this->pos_ <= 0) {
        return;
    }

    this->pos_--;
}

void Tape::Rewind() {
    this->file_.flush();
    this->file_.clear();
    this->pos_ = 0;
}

void Tape::Clear() {
    if (this->file_.is_open()) {
        this->file_.close();
    }
    this->file_.clear();
    this->file_.open(this->GetPath(), std::ios::in | std::ios::out |
                                          std::ios::trunc | std::ios::binary);
    if (!this->file_.is_open()) {
        throw std::runtime_error("Failed to clear tape file");
    }
    this->pos_ = 0;
}

void Tape::SetRecordMetrics(bool enabled) { this->recordMetrics_ = enabled; }

uint32_t Tape::GetId() const { return this->id_; }

uint32_t Tape::GetCurrentPosition() const { return this->pos_; };

std::string Tape::GetPath() const {
    return std::format("./tmp/tape_{}", this->id_);
}
