#pragma once

#include <fstream>
#include <memory>
#include <string>

#include "File.hpp"
#include "Item.hpp"
#include "PivotBuffer.hpp"

class QuickSortExterno {
  private:
    static void Executar(std::fstream &file, int Esq, int Dir);

    static void Particao(std::fstream &file, PivotBuffer &pivot, int Esq,
                         int Dir, int &i, int &j);

    static void LeSup(std::fstream &file, Item &UltLido, int &Ls,
                      bool &OndeLer);

    static void LeInf(std::fstream &file, Item &UltLido, int &Li,
                      bool &OndeLer);

    static void EscreveMax(std::fstream &file, const Item &R, int &Es);

    static void EscreveMin(std::fstream &file, const Item &R, int &Ei);

  public:
    static void Ordenar(const std::string &binFilePath, int quantity);
    static void Ordenar(const std::shared_ptr<File> &file);
    static int Run(const std::shared_ptr<File> &file);
};
