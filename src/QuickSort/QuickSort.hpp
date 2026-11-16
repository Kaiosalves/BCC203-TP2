#pragma once

#include <fstream>
#include <memory>
#include <string>

#include "File.hpp"
#include "PivotBuffer.hpp"

class QuickSortExterno {
  private:
    static void Executar(std::fstream &file, int Esq, int Dir);
    static void Particao(std::fstream &file, PivotBuffer &pivot, int Esq,
                         int Dir, int &i, int &j);

  public:
    static void Ordenar(const std::string &binFilePath, int quantity);
    static void Ordenar(const std::shared_ptr<File> &file);
    static int Run(const std::shared_ptr<File> &file);
};
