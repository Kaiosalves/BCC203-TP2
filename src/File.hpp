#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "Common.hpp"
#include "Item.hpp"

// Gerencia o arquivo texto de entrada, sua conversão para um arquivo binário
// intermediário de registros fixos, a leitura paginada durante a ordenação e
// a conversão final do arquivo binário ordenado de volta para texto.
class File {
  public:
    File(const std::string &path, uint64_t quantity,
         const std::string &outputPath = "");
    ~File();

    File(const File &) = delete;
    File &operator=(const File &) = delete;

    // Converte as primeiras `quantity_` linhas do arquivo texto de entrada para
    // o arquivo binário intermediário.
    void ConvertTextToBinary();

    // Converte o arquivo binário intermediário para o arquivo texto final
    // ordenado e retorna o caminho do arquivo gerado.
    std::string ConvertBinaryToText(const std::string &outputPath = "") const;

    std::array<Item, PAGE_SIZE> GetNextPage();
    bool GetNextItem(Item &out);
    void Rewind();
    void CloseStream();
    void ReopenStream();

    void SetRecordMetrics(bool enabled);
    void PrintContents();
    bool VerifySorted(bool print);
    std::string path() const;
    std::string binaryPath() const;
    std::string outputPath() const;
    std::filesystem::file_time_type lastModification() const;
    uint64_t size() const;
    uint64_t quantity() const;
    uint64_t lastPageCount() const;
    uint64_t itemsRead() const;
    bool eof();

    static Item ParseLine(std::string line);
    static std::string FormatProvaoLine(const Item &item);
    static std::string FormatDisplayLine(const Item &item);

  private:
    template <typename Fn> void ForEachPage(Fn &&fn);

    std::ifstream file_;
    std::string path_;
    std::string binaryPath_;
    std::string outputPath_;

    uint64_t quantity_;
    uint64_t itemsRead_ = 0;
    uint64_t lastPageCount_ = 0;
    bool recordMetrics_ = false;
};
