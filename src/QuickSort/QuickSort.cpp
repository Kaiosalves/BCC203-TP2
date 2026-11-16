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

    Executar(file, 1, quantity);
    file.flush();
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
        return result;
    });
}

// Função recursiva para executar o algoritmo principal.
void QuickSortExterno::Executar(std::fstream &file, int Esq, int Dir) {
    if (Dir - Esq < 1) {
        return;
    }

    PivotBuffer pivot;
    int i = 0;
    int j = 0;
    Particao(file, pivot, Esq, Dir, i, j);

    // Chama primeiro para o subarquivo de menor tamanho.
    if (i - Esq < Dir - j) {
        Executar(file, Esq, i);
        Executar(file, j, Dir);
    } else {
        Executar(file, j, Dir);
        Executar(file, Esq, i);
    }
}
