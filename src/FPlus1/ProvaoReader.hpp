
#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "Item.hpp"

// Lê até `limit` registros do arquivo do Provão.
// Colunas: inscrição (1-8), nota (10-14), estado (16-17), cidade (19-68)
// e curso (70-99).
class ProvaoReader {
  public:
    ProvaoReader(const std::string &path, uint64_t limit);

    // Lê o próximo registro. Retorna false no limite ou no fim do arquivo.
    bool Next(Item &out);

  private:
    void ParseLine(Item &out);

    std::ifstream in_;
    std::string path_;
    uint64_t limit_;
    uint64_t delivered_ = 0;
    uint64_t lineNumber_ = 0;
    std::string line_;
};
