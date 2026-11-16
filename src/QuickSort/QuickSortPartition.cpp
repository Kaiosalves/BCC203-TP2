#include "QuickSort.hpp"

#include <limits>

#include "Common.hpp"
#include "Item.hpp"

namespace {

std::streamoff ItemByteOffset(int oneBasedPos) {
    return static_cast<std::streamoff>(oneBasedPos - 1) *
           static_cast<std::streamoff>(sizeof(Item));
}

void ReadBoundaryItem(std::fstream &file, Item &out, int &cursor, int step,
                      bool &readFromUpper, bool nextReadFromUpper) {
    file.clear();
    file.seekg(ItemByteOffset(cursor), std::ios::beg);
    file.read(reinterpret_cast<char *>(&out), sizeof(Item));
    Metrics::RecordDiskRead();

    cursor += step;
    readFromUpper = nextReadFromUpper;
}

void ReadNextBoundaryItem(std::fstream &file, Item &out, int &ls, int &li,
                          bool &ondeLer, bool readUpper) {
    if (readUpper) {
        ReadBoundaryItem(file, out, ls, -1, ondeLer, false);
    } else {
        ReadBoundaryItem(file, out, li, +1, ondeLer, true);
    }
}

void WriteBoundaryItem(std::fstream &file, const Item &item, int &cursor,
                       int step) {
    file.clear();
    file.seekp(ItemByteOffset(cursor), std::ios::beg);
    file.write(reinterpret_cast<const char *>(&item), sizeof(Item));
    Metrics::RecordDiskWrite();

    cursor += step;
}

}  // namespace

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
        if (pivot.OccupiedCount() < TAMAREA - 1) {
            ReadNextBoundaryItem(file, UltLido, Ls, Li, OndeLer, OndeLer);
            pivot.Insert(UltLido);
            continue;
        }

        // Garante que a leitura não alcance a escrita.
        const bool readUpper = (Ls == Es) || (Li != Ei && OndeLer);
        ReadNextBoundaryItem(file, UltLido, Ls, Li, OndeLer, readUpper);

        // Maior que o limite superior: vai direto para A2.
        Metrics::RecordKeyComparison();
        if (UltLido.grade > Lsup) {
            j = Es;
            WriteBoundaryItem(file, UltLido, Es, -1);
            continue;
        }

        // Menor que o limite inferior: vai direto para A1.
        Metrics::RecordKeyComparison();
        if (UltLido.grade < Linf) {
            i = Ei;
            WriteBoundaryItem(file, UltLido, Ei, +1);
            continue;
        }

        // Está entre os limites: insere na área interna e remove do menor
        // subarquivo.
        pivot.Insert(UltLido);
        if (Ei - Esq < Dir - Es) {
            R = pivot.MinRemove();
            WriteBoundaryItem(file, R, Ei, +1);
            Linf = R.grade;
        } else {
            R = pivot.MaxRemove();
            WriteBoundaryItem(file, R, Es, -1);
            Lsup = R.grade;
        }
    }

    // Descarrega a área interna no meio do arquivo, já ordenada.
    while (Ei <= Es) {
        R = pivot.MinRemove();
        WriteBoundaryItem(file, R, Ei, +1);
    }

    file.flush();
}
