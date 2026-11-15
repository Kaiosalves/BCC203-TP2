#include "Tape.hpp"
#include "Common.hpp"
#include <cstdint>
#include <filesystem>
#include <algorithm>

uint32_t Tape::lastId_ = 0;
uint64_t Tape::readTransfers = 0;
uint64_t Tape::writeTransfers = 0;

Tape::Tape() {
    this->id_ = Tape::lastId_++;

    this->file_ =
        std::fstream(this->GetPath(), std::ios::in | std::ios::out |
                                          std::ios::trunc | std::ios::binary);

    if (!this->file_.is_open()) {
        throw std::runtime_error("Failed to open tape file");
    }
}

Tape::~Tape() {
    this->file_.close();
    std::filesystem::remove(this->GetPath());
}

std::array<Item, PAGE_SIZE> Tape::ReadBlock() {
    this->file_.clear();
    this->file_.seekg(sizeof(std::array<Item, PAGE_SIZE>) * this->pos_);

    std::array<Item, PAGE_SIZE> block{};
    this->file_.read(reinterpret_cast<char *>(&block),
                     sizeof(std::array<Item, PAGE_SIZE>));
    this->AdvanceBlock();
    readTransfers++;
    return block;
}

void Tape::WriteBlock(std::array<Item, PAGE_SIZE> block) {
    this->file_.clear();
    this->file_.seekp(sizeof(std::array<Item, PAGE_SIZE>) * this->pos_);

    this->file_.write(reinterpret_cast<char *>(&block),
                      sizeof(std::array<Item, PAGE_SIZE>));
    this->AdvanceBlock();
    writeTransfers++;
}

void Tape::AdvanceBlock() { this->pos_++; }

void Tape::RetreatBlock() {
    if (this->pos_ <= 0) {
        return;
    }
    this->pos_--;
}

uint32_t Tape::GetId() const { return this->id_; }

uint32_t Tape::GetCurrentPosition() const { return this->pos_; }

std::string Tape::GetPath() const {
    return "./tmp/tape_" + std::to_string(this->id_);
}

void Tape::BeginRead() {
    this->file_.clear();
    this->pos_ = 0;
    this->currentRunIndex_ = 0;
    this->itemsReadInRun_ = 0;
    this->readBufferIndex_ = 0;
    this->readBufferCount_ = 0;
}

void Tape::BeginWrite() {
    this->file_.clear();
    this->pos_ = 0;
    this->runLengths_.clear();
    this->writeBufferIndex_ = 0;
    this->writeItemCount_ = 0;
}

void Tape::WriteItem(const Item& item) {
    writeBuffer_[writeBufferIndex_++] = item;
    writeItemCount_++;
    if (writeBufferIndex_ == PAGE_SIZE) {
        WriteBlock(writeBuffer_);
        writeBufferIndex_ = 0;
    }
}

void Tape::EndRun() {
    if (writeBufferIndex_ > 0) {
        WriteBlock(writeBuffer_);
        writeBufferIndex_ = 0;
    }
    runLengths_.push_back(writeItemCount_);
    writeItemCount_ = 0;
}

bool Tape::StartNextRun() {
    if (currentRunIndex_ >= runLengths_.size()) {
        return false;
    }
    itemsReadInRun_ = 0;
    readBufferIndex_ = 0;
    readBufferCount_ = 0;
    return true;
}

bool Tape::ReadItem(Item& out) {
    if (currentRunIndex_ >= runLengths_.size()) {
        return false;
    }
    uint64_t currentRunLen = runLengths_[currentRunIndex_];
    if (itemsReadInRun_ >= currentRunLen) {
        currentRunIndex_++;
        return false;
    }

    if (readBufferIndex_ >= readBufferCount_) {
        readBuffer_ = ReadBlock();
        readBufferIndex_ = 0;
        uint64_t remainingInRun = currentRunLen - itemsReadInRun_;
        readBufferCount_ = std::min(static_cast<uint64_t>(PAGE_SIZE), remainingInRun);
    }

    out = readBuffer_[readBufferIndex_++];
    itemsReadInRun_++;

    if (itemsReadInRun_ == currentRunLen) {
        currentRunIndex_++;
    }
    return true;
}

uint64_t Tape::RunCount() const {
    if (currentRunIndex_ >= runLengths_.size()) {
        return 0;
    }
    return runLengths_.size() - currentRunIndex_;
}