#include "QuickSort.hpp"

#include <array>
#include <cstdint>
#include <limits>

#include "Common.hpp"
#include "Item.hpp"

namespace {

std::streamoff ItemByteOffset(int oneBasedPos) {
    return static_cast<std::streamoff>(oneBasedPos - 1) *
           static_cast<std::streamoff>(sizeof(Item));
}

void ReadBoundaryItem(std::fstream &file, Item &out, int &cursor, int step,
                      bool &readFromUpper, bool nextReadFromUpper,
                      uint64_t &recordReads) {
    file.clear();
    file.seekg(ItemByteOffset(cursor), std::ios::beg);
    file.read(reinterpret_cast<char *>(&out), sizeof(Item));
    ++recordReads;

    cursor += step;
    readFromUpper = nextReadFromUpper;
}

void ReadNextBoundaryItem(std::fstream &file, Item &out, int &ls, int &li,
                          bool &ondeLer, bool readUpper,
                          uint64_t &recordReads) {
    if (readUpper) {
        ReadBoundaryItem(file, out, ls, -1, ondeLer, false, recordReads);
    } else {
        ReadBoundaryItem(file, out, li, +1, ondeLer, true, recordReads);
    }
}

void WriteBoundaryItem(std::fstream &file, const Item &item, int &cursor,
                       int step, uint64_t &recordWrites) {
    file.clear();
    file.seekp(ItemByteOffset(cursor), std::ios::beg);
    file.write(reinterpret_cast<const char *>(&item), sizeof(Item));
    ++recordWrites;

    cursor += step;
}

}  // namespace

void QuickSortExterno::SortSmallPartition(std::fstream &file, int Esq, int Dir,
                                          TransferStats &stats) {
    const int count = Dir - Esq + 1;
    std::array<Item, TAMAREA> buffer{};

    file.clear();
    file.seekg(ItemByteOffset(Esq), std::ios::beg);
    file.read(reinterpret_cast<char *>(buffer.data()),
              static_cast<std::streamsize>(count) *
                  static_cast<std::streamsize>(sizeof(Item)));
    stats.recordReads += static_cast<uint64_t>(count);

    // Ordenação interna por inserção quando |S| <= TAMAREA.
    for (int k = 1; k < count; ++k) {
        const Item key = buffer[k];
        int pos = k - 1;
        while (pos >= 0) {
            Metrics::RecordKeyComparison();
            if (key.grade < buffer[pos].grade) {
                buffer[pos + 1] = buffer[pos];
                --pos;
            } else {
                break;
            }
        }
        buffer[pos + 1] = key;
    }

    file.clear();
    file.seekp(ItemByteOffset(Esq), std::ios::beg);
    file.write(reinterpret_cast<const char *>(buffer.data()),
               static_cast<std::streamsize>(count) *
                   static_cast<std::streamsize>(sizeof(Item)));
    stats.recordWrites += static_cast<uint64_t>(count);
    file.flush();
}

void QuickSortExterno::Particao(std::fstream &file, PivotBuffer &pivot, int Esq,
                                int Dir, int &i, int &j,
                                TransferStats &stats) {
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
        if (pivot.OccupiedCount() < TAMAREA - 1) {
            ReadNextBoundaryItem(file, UltLido, Ls, Li, OndeLer, OndeLer,
                                 stats.recordReads);
            pivot.Insert(UltLido);
            continue;
        }

        // Garante que a leitura não alcance a escrita.
        const bool readUpper = (Ls == Es) || (Li != Ei && OndeLer);
        ReadNextBoundaryItem(file, UltLido, Ls, Li, OndeLer, readUpper,
                             stats.recordReads);

        // Maior que o limite superior: vai direto para A2.
        Metrics::RecordKeyComparison();
        if (UltLido.grade > Lsup) {
            j = Es;
            WriteBoundaryItem(file, UltLido, Es, -1, stats.recordWrites);
            continue;
        }

        // Menor que o limite inferior: vai direto para A1.
        Metrics::RecordKeyComparison();
        if (UltLido.grade < Linf) {
            i = Ei;
            WriteBoundaryItem(file, UltLido, Ei, +1, stats.recordWrites);
            continue;
        }

        // Está entre os limites: insere na área interna e remove do menor
        // subarquivo.
        pivot.Insert(UltLido);
        if (Ei - Esq < Dir - Es) {
            R = pivot.MinRemove();
            WriteBoundaryItem(file, R, Ei, +1, stats.recordWrites);
            Linf = R.grade;
        } else {
            R = pivot.MaxRemove();
            WriteBoundaryItem(file, R, Es, -1, stats.recordWrites);
            Lsup = R.grade;
        }
    }

    // Descarrega a área interna no meio do arquivo, já ordenada.
    while (Ei <= Es) {
        R = pivot.MinRemove();
        WriteBoundaryItem(file, R, Ei, +1, stats.recordWrites);
    }

    file.flush();
}
