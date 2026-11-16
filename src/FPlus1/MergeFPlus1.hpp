#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "RunTape.hpp"
#include "SelectionHeap.hpp"
#include "SortStats.hpp"

// Método 2: intercalação balanceada f+1 com seleção por substituição.
//
// Recursos:
//   - 19 registros em memória;
//   - 19 fitas de entrada e uma de saída.
//
// Os blocos iniciais são distribuídos entre as fitas de entrada. Em cada
// passada, os blocos correspondentes são intercalados na fita de saída e,
// se necessário, redistribuídos para a próxima passada.
class BalancedMergeFPlus1 {
  public:
    static constexpr std::size_t kMemoryRecords = 19;
    static constexpr std::size_t kInputTapes = 19;
    static constexpr std::size_t kTotalTapes = kInputTapes + 1;

    BalancedMergeFPlus1(std::string inputPath, uint64_t quantity);

    // Ordena os registros e atualiza as estatísticas.
    void Sort();

    const SortStats &stats() const { return stats_; }

    // Confere o resultado e, se solicitado, imprime os registros ordenados.
    // Deve ser chamado após Sort().
    bool CheckAndShowResult(bool print);

  private:
    void GenerateRuns();
    void MergePass();
    void Redistribute();
    bool AnyInputHasRun() const;
    static void ReadOrThrow(RunTape &tape, Item &item);

    std::string inputPath_;
    uint64_t quantity_;
    SortStats stats_;

    std::vector<std::unique_ptr<RunTape>> tapes_;
    FMais1::SelectionHeap<kMemoryRecords> heap_;
    RunTape *result_ = nullptr;
};

  // Executa o método 2. Retorna 0 em caso de sucesso e -1 em caso de erro.
int RunMethod2(const std::string &inputPath, uint64_t quantity);
