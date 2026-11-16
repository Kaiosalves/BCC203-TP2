#include "QuickSort.hpp"

#include <chrono>
#include <stdexcept>

#include "Common.hpp"
#include "MethodRunner.hpp"

void QuickSortExterno::Ordenar(const std::string &binFilePath, int quantity) {
    std::fstream file(binFilePath,
                      std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Falha ao abrir arquivo binário: " +
                                 binFilePath);
    }

    TransferStats stats{};
    Executar(file, 1, quantity, stats);
    file.flush();

    // Converte o total de registros lidos/escritos para blocos de PAGE_SIZE
    // registros (arredondamento para cima), alinhando a métrica aos métodos 1 e 2.
    constexpr uint64_t blockSize = static_cast<uint64_t>(PAGE_SIZE);
    const uint64_t readBlocks = (stats.recordReads + blockSize - 1) / blockSize;
    const uint64_t writeBlocks =
        (stats.recordWrites + blockSize - 1) / blockSize;
    Metrics::RecordDiskRead(readBlocks);
    Metrics::RecordDiskWrite(writeBlocks);
}

void QuickSortExterno::Ordenar(const std::shared_ptr<File> &file) {
    if (file == nullptr) {
        throw std::invalid_argument("Arquivo de entrada inválido");
    }

    file->CloseStream();
    Metrics::Reset();
    Metrics::SetPhase(Phase::Search);
    Ordenar(file->binaryPath(), static_cast<int>(file->quantity()));
    file->ReopenStream();
}

int QuickSortExterno::Run(const std::shared_ptr<File> &file) {
    return RunSortMethod(file, [](const std::shared_ptr<File> &inputFile) {
        Metrics::Reset();
        inputFile->CloseStream();

        Metrics::SetPhase(Phase::Search);
        const auto p2Start = std::chrono::steady_clock::now();
        Ordenar(inputFile->binaryPath(),
                static_cast<int>(inputFile->quantity()));
        const auto p2End = std::chrono::steady_clock::now();

        inputFile->ReopenStream();

        SortRunResult result;
        result.phase1TimeMs = 0.0;
        result.phase2TimeMs =
            std::chrono::duration<double, std::milli>(p2End - p2Start).count();
        result.report.title = "=== Método 3: Quicksort Externo ===";
        result.report.readLabel = "Transferências de leitura (blocos):";
        result.report.writeLabel = "Transferências de escrita (blocos):";
        return result;
    });
}

// Função recursiva para executar o algoritmo principal.
void QuickSortExterno::Executar(std::fstream &file, int Esq, int Dir,
                                TransferStats &stats) {
    if (Dir <= Esq) {
        return;
    }
    if (Dir - Esq + 1 <= TAMAREA) {
        SortSmallPartition(file, Esq, Dir, stats);
        return;
    }

    PivotBuffer pivot;
    int i = 0;
    int j = 0;
    Particao(file, pivot, Esq, Dir, i, j, stats);

    // Chama primeiro para o subarquivo de menor tamanho.
    if (i - Esq < Dir - j) {
        Executar(file, Esq, i, stats);
        Executar(file, j, Dir, stats);
    } else {
        Executar(file, j, Dir, stats);
        Executar(file, Esq, i, stats);
    }
}
