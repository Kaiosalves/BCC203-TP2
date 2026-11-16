
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Item.hpp"
#include "SortStats.hpp"

// Arquivo binário que representa uma fita com blocos ordenados.
// Os tamanhos dos blocos ficam em memória, sem marcadores no arquivo.
class RunTape {
  public:
    RunTape(std::filesystem::path path, SortStats *stats);
    ~RunTape();

    RunTape(const RunTape &) = delete;
    RunTape &operator=(const RunTape &) = delete;

    // Apaga o conteúdo e prepara a fita para escrita.
    void Clear();

    // Escreve um registro no bloco atual.
    void WriteRecord(const Item &item);

    // Encerra o bloco corrente (se não estiver vazio).
    void EndRun();

    // Volta ao início para leitura.
    void Rewind();

    // Lê o próximo registro. Retorna false se não houver mais registros.
    bool ReadRecord(Item &item);

    // Há mais um bloco a ser lido?
    bool HasNextRun() const;

    // Avança para o próximo bloco e devolve a quantidade de registros dele.
    uint64_t BeginNextRun();

    // Quantidade de blocos completos gravados na fita.
    std::size_t NumRuns() const;

    // Define o contador; nullptr desativa a contagem.
    void SetStats(SortStats *stats) { stats_ = stats; }

  private:
    std::filesystem::path path_;
    std::fstream file_;
    SortStats *stats_;

    std::vector<uint64_t> runs_;
    uint64_t currentRunLength_ = 0;
    std::size_t nextRun_ = 0;
};
