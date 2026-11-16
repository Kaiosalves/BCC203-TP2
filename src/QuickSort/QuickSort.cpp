#include "QuickSort.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "Common.hpp"

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
    try {
        if (file == nullptr) {
            throw std::invalid_argument("Arquivo de entrada inválido");
        }

        const bool print = Log::enableInfo;
        if (print) {
            file->PrintContents();
        }

        Metrics::Reset();
        file->CloseStream();

        constexpr double phase1TimeMs = 0.0;

        Metrics::SetPhase(Phase::Search);
        const auto p2Start = std::chrono::steady_clock::now();
        Ordenar(file->binaryPath(), static_cast<int>(file->quantity()));
        const auto p2End = std::chrono::steady_clock::now();
        const double phase2TimeMs =
            std::chrono::duration<double, std::milli>(p2End - p2Start).count();

        file->ReopenStream();

        const PhaseMetrics phase1 = Metrics::Preprocessing();
        const PhaseMetrics phase2 = Metrics::Search();

        const bool ok = file->VerifySorted(print);
        if (!ok) {
            Log::Error("A verificação do resultado falhou: o arquivo não "
                       "ficou ordenado corretamente");
            return -1;
        }

        const std::string outputPath = file->ConvertBinaryToText();
        Log::Info("Arquivo texto final ordenado gerado em: " + outputPath);

        const uint64_t totalReads = phase1.diskReads + phase2.diskReads;
        const uint64_t totalWrites = phase1.diskWrites + phase2.diskWrites;
        const uint64_t totalComparisons =
            phase1.keyComparisons + phase2.keyComparisons;
        const double totalSeconds = (phase1TimeMs + phase2TimeMs) / 1000.0;

        char seconds[32];
        std::snprintf(seconds, sizeof(seconds), "%.6f", totalSeconds);

        std::cout << "=== Método 3: Quicksort Externo ===\n"
                  << "Registros ordenados:               " << file->quantity()
                  << '\n'
                  << "Transferências de leitura:         " << totalReads << '\n'
                  << "Transferências de escrita:         " << totalWrites
                  << '\n'
                  << "Comparações entre chaves:          " << totalComparisons
                  << '\n'
                  << "Tempo de execução (s):             " << seconds << '\n'
                  << "{\"phase1_time_ms\":" << phase1TimeMs
                  << ",\"phase1_reads\":" << phase1.diskReads
                  << ",\"phase1_writes\":" << phase1.diskWrites
                  << ",\"phase2_time_ms\":" << phase2TimeMs
                  << ",\"phase2_reads\":" << phase2.diskReads
                  << ",\"comparisons\":" << totalComparisons << "}\n";
        return 0;
    } catch (const std::exception &e) {
        Log::Error(e.what());
        return -1;
    }
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

void QuickSortExterno::LeSup(std::fstream &file, Item &UltLido, int &Ls,
                             bool &OndeLer) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(Ls - 1) *
                   static_cast<std::streamoff>(sizeof(Item)),
               std::ios::beg);
    file.read(reinterpret_cast<char *>(&UltLido), sizeof(Item));
    Metrics::RecordDiskRead();

    Ls--;
    OndeLer = false;
}

void QuickSortExterno::LeInf(std::fstream &file, Item &UltLido, int &Li,
                             bool &OndeLer) {
    file.clear();
    file.seekg(static_cast<std::streamoff>(Li - 1) *
                   static_cast<std::streamoff>(sizeof(Item)),
               std::ios::beg);
    file.read(reinterpret_cast<char *>(&UltLido), sizeof(Item));
    Metrics::RecordDiskRead();

    Li++;
    OndeLer = true;
}

void QuickSortExterno::EscreveMax(std::fstream &file, const Item &R, int &Es) {
    file.clear();
    file.seekp(static_cast<std::streamoff>(Es - 1) *
                   static_cast<std::streamoff>(sizeof(Item)),
               std::ios::beg);
    file.write(reinterpret_cast<const char *>(&R), sizeof(Item));
    Metrics::RecordDiskWrite();

    Es--;
}

void QuickSortExterno::EscreveMin(std::fstream &file, const Item &R, int &Ei) {
    file.clear();
    file.seekp(static_cast<std::streamoff>(Ei - 1) *
                   static_cast<std::streamoff>(sizeof(Item)),
               std::ios::beg);
    file.write(reinterpret_cast<const char *>(&R), sizeof(Item));
    Metrics::RecordDiskWrite();

    Ei++;
}

void QuickSortExterno::Particao(std::fstream &file, PivotBuffer &pivot, int Esq,
                                int Dir, int &i, int &j) {
    int Ls = Dir;
    int Es = Dir;
    int Li = Esq;
    int Ei = Esq;
    float Linf = std::numeric_limits<float>::lowest();
    float Lsup = std::numeric_limits<float>::max();
    bool OndeLer = true;
    Item UltLido{};
    Item R{};

    pivot.ClearVector();
    i = Esq - 1;
    j = Dir + 1;

    while (Ls >= Li) {
        if (pivot.OccupiedCOunt() < TAMAREA - 1) {
            if (OndeLer) {
                LeSup(file, UltLido, Ls, OndeLer);
            } else {
                LeInf(file, UltLido, Li, OndeLer);
            }
            pivot.Insert(UltLido);
            continue;
        }

        // Garante que a leitura não alcance a escrita.
        if (Ls == Es) {
            LeSup(file, UltLido, Ls, OndeLer);
        } else if (Li == Ei) {
            LeInf(file, UltLido, Li, OndeLer);
        } else if (OndeLer) {
            LeSup(file, UltLido, Ls, OndeLer);
        } else {
            LeInf(file, UltLido, Li, OndeLer);
        }

        // Maior que o limite superior: vai direto para A2.
        Metrics::RecordKeyComparison();
        if (UltLido.grade > Lsup) {
            j = Es;
            EscreveMax(file, UltLido, Es);
            continue;
        }

        // Menor que o limite inferior: vai direto para A1.
        Metrics::RecordKeyComparison();
        if (UltLido.grade < Linf) {
            i = Ei;
            EscreveMin(file, UltLido, Ei);
            continue;
        }

        // Está entre os limites: insere na área interna e remove do menor
        // subarquivo.
        pivot.Insert(UltLido);
        if (Ei - Esq < Dir - Es) {
            R = pivot.MinRemove();
            EscreveMin(file, R, Ei);
            Linf = R.grade;
        } else {
            R = pivot.MaxRemove();
            EscreveMax(file, R, Es);
            Lsup = R.grade;
        }
    }

    // Descarrega a área interna no meio do arquivo, já ordenada.
    while (Ei <= Es) {
        R = pivot.MinRemove();
        EscreveMin(file, R, Ei);
    }

    file.flush();
}
