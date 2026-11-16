#include "QuickSort.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "Common.hpp"

namespace {

std::string TrimRight(const char *data, std::size_t size) {
    std::string text(data, size);
    const auto last = text.find_last_not_of(" \0", std::string::npos, 2);
    text.erase(last == std::string::npos ? 0 : last + 1);
    return text;
}

std::string FormatItem(const Item &item) {
    char head[64];
    std::snprintf(head, sizeof(head), "%08ld %5.1f %.2s ",
                  item.registrationNumber, static_cast<double>(item.grade),
                  item.state.data());
    return std::string(head) + TrimRight(item.city.data(), item.city.size()) +
           " | " + TrimRight(item.degree.data(), item.degree.size());
}

void PrintInput(const std::shared_ptr<File> &file) {
    std::cout << "--- Dados a serem ordenados (" << file->quantity()
              << " registros) ---\n";
    while (!file->eof()) {
        const auto page = file->GetNextPage();
        const uint64_t count = file->lastPageCount();
        for (uint64_t i = 0; i < count; ++i) {
            std::cout << FormatItem(page[i]) << '\n';
        }
    }
    std::cout << std::flush;
    file->Rewind();
}

struct TempBinaryFileGuard {
    std::filesystem::path path;
    ~TempBinaryFileGuard() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

void CopyFileToTempBinary(const std::shared_ptr<File> &file,
                          const std::filesystem::path &tempPath) {
    file->Rewind();
    std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Falha ao criar arquivo temporário do Quicksort Externo");
    }

    uint64_t copied = 0;
    while (!file->eof() && copied < file->quantity()) {
        const auto page = file->GetNextPage();
        const uint64_t count = file->lastPageCount();
        if (count == 0) {
            break;
        }
        out.write(reinterpret_cast<const char *>(page.data()),
                  static_cast<std::streamsize>(count * sizeof(Item)));
        if (!out) {
            throw std::runtime_error(
                "Falha ao gravar arquivo temporário do Quicksort Externo");
        }
        Metrics::RecordDiskWrite();
        copied += count;
    }

    if (copied < file->quantity()) {
        throw std::runtime_error("O arquivo de entrada possui apenas " +
                                 std::to_string(copied) +
                                 " registros, mas foram pedidos " +
                                 std::to_string(file->quantity()));
    }
}

bool VerifyAndShowBinaryResult(const std::filesystem::path &binPath,
                               uint64_t quantity, bool print) {
    std::ifstream in(binPath, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    if (print) {
        std::cout << "--- Resultado da ordenação ---\n";
    }

    Item item{};
    Item previous{};
    uint64_t count = 0;
    bool ordered = true;

    while (count < quantity &&
           in.read(reinterpret_cast<char *>(&item), sizeof(Item))) {
        if (count > 0 && item.grade < previous.grade) {
            ordered = false;
        }
        if (print) {
            std::cout << FormatItem(item) << '\n';
        }
        previous = item;
        ++count;
    }

    if (print) {
        std::cout << std::flush;
    }

    return ordered && count == quantity;
}

}  // namespace

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

    std::filesystem::create_directories("./tmp");
    const std::string tag = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const TempBinaryFileGuard tempGuard{"./tmp/quicksort_" + tag + ".bin"};

    Metrics::Reset();
    Metrics::SetPhase(Phase::Preprocessing);
    CopyFileToTempBinary(file, tempGuard.path);

    Metrics::SetPhase(Phase::Search);
    Ordenar(tempGuard.path.string(), static_cast<int>(file->quantity()));
}

int QuickSortExterno::Run(const std::shared_ptr<File> &file) {
    try {
        if (file == nullptr) {
            throw std::invalid_argument("Arquivo de entrada inválido");
        }

        const bool print = Log::enableInfo;
        if (print) {
            PrintInput(file);
        }

        std::filesystem::create_directories("./tmp");
        const std::string tag = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const TempBinaryFileGuard tempGuard{"./tmp/quicksort_" + tag + ".bin"};

        Metrics::Reset();
        Metrics::SetPhase(Phase::Preprocessing);

        const auto p1Start = std::chrono::steady_clock::now();
        CopyFileToTempBinary(file, tempGuard.path);
        const auto p1End = std::chrono::steady_clock::now();
        const double phase1TimeMs =
            std::chrono::duration<double, std::milli>(p1End - p1Start).count();

        Log::Info("Arquivo binário temporário preparado para Quicksort Externo");

        Metrics::SetPhase(Phase::Search);
        const auto p2Start = std::chrono::steady_clock::now();
        Ordenar(tempGuard.path.string(), static_cast<int>(file->quantity()));
        const auto p2End = std::chrono::steady_clock::now();
        const double phase2TimeMs =
            std::chrono::duration<double, std::milli>(p2End - p2Start).count();

        const PhaseMetrics phase1 = Metrics::Preprocessing();
        const PhaseMetrics phase2 = Metrics::Search();

        const bool ok =
            VerifyAndShowBinaryResult(tempGuard.path, file->quantity(), print);
        if (!ok) {
            Log::Error("A verificação do resultado falhou: o arquivo não "
                       "ficou ordenado corretamente");
            return -1;
        }

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
