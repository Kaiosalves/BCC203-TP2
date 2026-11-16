#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Common.hpp"
#include "File.hpp"
#include "Item.hpp"
#include "SelectionHeap.hpp"
#include "Tape.hpp"

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

    explicit BalancedMergeFPlus1(std::shared_ptr<File> file);

    // Ordena os registros e atualiza as métricas globais.
    void Sort();

    uint64_t runs() const { return runs_; }
    uint64_t mergePasses() const { return mergePasses_; }
    double phase1TimeMs() const { return phase1TimeMs_; }
    double phase2TimeMs() const { return phase2TimeMs_; }

    // Confere o resultado e, se solicitado, imprime os registros ordenados.
    // Deve ser chamado após Sort().
    bool CheckAndShowResult(bool print);

  private:
    struct BufferedTape {
        std::unique_ptr<Tape> tape;
        std::array<Item, PAGE_SIZE> buffer{};
        std::size_t bufPos = 0;
        std::vector<uint64_t> runs;
        uint64_t currentRunLength = 0;
        uint64_t totalRecords = 0;
        uint64_t recordsRead = 0;
        std::size_t nextRun = 0;
        bool reading = false;

        BufferedTape();

        void Clear();
        void WriteRecord(const Item &item);
        void EndRun();
        void Rewind();
        bool ReadRecord(Item &item);
        bool HasNextRun() const;
        uint64_t BeginNextRun();
        std::size_t NumRuns() const;
    };

    void GenerateRuns();
    void MergePass();
    void Redistribute();
    void WriteResultToBinary();
    bool AnyInputHasRun() const;
    static void ReadOrThrow(BufferedTape &tape, Item &item);

    std::shared_ptr<File> file_;
    uint64_t runs_ = 0;
    uint64_t mergePasses_ = 0;
    double phase1TimeMs_ = 0.0;
    double phase2TimeMs_ = 0.0;

    std::vector<std::unique_ptr<BufferedTape>> tapes_;
    FMais1::SelectionHeap<kMemoryRecords> heap_;
    BufferedTape *result_ = nullptr;
};

// Executa o método 2. Retorna 0 em caso de sucesso e -1 em caso de erro.
int RunMethod2(const std::shared_ptr<File> &file);
