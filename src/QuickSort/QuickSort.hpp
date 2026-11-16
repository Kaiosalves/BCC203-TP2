#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

#include "File.hpp"
#include "PivotBuffer.hpp"

class QuickSortExterno {
  private:
    struct TransferStats {
        uint64_t recordReads = 0;
        uint64_t recordWrites = 0;
    };

    static void Executar(std::fstream &file, int Esq, int Dir,
                         TransferStats &stats);
    static void SortSmallPartition(std::fstream &file, int Esq, int Dir,
                                   TransferStats &stats);
    static void Particao(std::fstream &file, PivotBuffer &pivot, int Esq,
                         int Dir, int &i, int &j, TransferStats &stats);

  public:
    static void Ordenar(const std::string &binFilePath, int quantity);
    static void Ordenar(const std::shared_ptr<File> &file);
    static int Run(const std::shared_ptr<File> &file);
};
