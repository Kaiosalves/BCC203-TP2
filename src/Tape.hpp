#pragma once

#include "Common.hpp"
#include "Item.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <vector>

class Tape {
  public:
    Tape();
    ~Tape();

    std::array<Item, PAGE_SIZE> ReadBlock();
    void WriteBlock(std::array<Item, PAGE_SIZE> block);

    void AdvanceBlock();
    void RetreatBlock();

    uint32_t GetId() const;
    uint32_t GetCurrentPosition() const;

    // Gerenciamento de runs e itens individuais
    void BeginRead();
    void BeginWrite();
    void WriteItem(const Item& item);
    void EndRun();
    bool StartNextRun();
    bool ReadItem(Item& out);
    uint64_t RunCount() const;

    // Estatísticas de transferência
    static uint64_t readTransfers;
    static uint64_t writeTransfers;

  private:
    static uint32_t lastId_;

    uint32_t id_;
    uint32_t pos_ = 0;
    std::fstream file_;

    std::string GetPath() const;

    std::vector<uint64_t> runLengths_;
    uint64_t currentRunIndex_ = 0;

    std::array<Item, PAGE_SIZE> writeBuffer_{};
    size_t writeBufferIndex_ = 0;
    uint64_t writeItemCount_ = 0;

    std::array<Item, PAGE_SIZE> readBuffer_{};
    size_t readBufferIndex_ = 0;
    size_t readBufferCount_ = 0;
    uint64_t itemsReadInRun_ = 0;
};