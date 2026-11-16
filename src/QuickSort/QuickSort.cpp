#include "QuickSort.hpp"
#include "Common.hpp"

void QuickSortExterno::Ordenar (const std::string& binFilePath, int quantity){
//cria os apontadores de leitura e escrita do arquivo
    std::fstream ArqLi(binFilePath, std::ios::in | std::ios::out | std::ios::binary);
    std::fstream ArqEi(binFilePath, std::ios::in | std::ios::out | std::ios::binary);
    std::fstream ArqLEs(binFilePath, std::ios::in | std::ios::out | std::ios::binary);
    
//chama o metodo principal pra executar o metodo
    Executar(ArqLi, ArqEi, ArqLEs, 1, quantity);
}


//funcao recursiva para executar o metodo principal do algoritmos
void QuickSortExterno::Executar(std::fstream& ArqLi, std::fstream& ArqEi, std::fstream& ArqLEs, int Esq, int Dir){
//nao precisa mais ordenar, caso base
    if (Dir - Esq < 1) return;
//cria a area de memoria interna
    PivotBuffer pivot;
    int i, j;
//particiona o arquivo
    Particao(ArqLi, ArqEi, ArqLEs, pivot, Esq, Dir, i, j);

//chama primeiro para o subarquivo de menor tamanho
    if (i - Esq < Dir - j) { 
        Executar(ArqLi, ArqEi, ArqLEs, Esq, i);
        Executar(ArqLi, ArqEi, ArqLEs, j, Dir);
    } else {
        Executar(ArqLi, ArqEi, ArqLEs, j, Dir);
        Executar(ArqLi, ArqEi, ArqLEs, Esq, i);
    }
}


void QuickSortExterno::LeSup(std::fstream& ArqLEs, Item& UltLido, int& Ls, bool& OndeLer){
//posiciona o ponteiro para leitura e armazena em ultlido
    ArqLEs.seekg((Ls - 1) * sizeof(Item), std::ios::beg);
    ArqLEs.read(reinterpret_cast<char*>(&UltLido), sizeof(Item));

//recua o ap para uma pos a esq
    Ls--;
    OndeLer = false;
}

void QuickSortExterno::LeInf(std::fstream& ArqLi, Item& UltLido, int& Li, bool& OndeLer){
//posiciona o ponteiro para leitura e armazena em ultlido

    ArqLi.seekg((Li - 1) * sizeof(Item), std::ios::beg);
    ArqLi.read(reinterpret_cast<char*>(&UltLido), sizeof(Item));

//avanca o ap para uma pos a dir
    Li++;
    OndeLer = true;
}

void QuickSortExterno::EscreveMax(std::fstream& ArqLEs, const Item& R, int& Es){
//posiciona o ponteiro para escrita na pos Es com o item R
    ArqLEs.seekp((Es - 1) * sizeof(Item), std::ios::beg);
    ArqLEs.write(reinterpret_cast<const char*>(&R), sizeof(Item));

 //recua o ap para uma pos a esq   
    Es--;
}

void QuickSortExterno::EscreveMin(std::fstream& ArqEi, const Item& R, int& Ei){
//posiciona o ponteiro para escrita na pos Ei com o item R
    ArqEi.seekp((Ei - 1) * sizeof(Item), std::ios::beg);
    ArqEi.write(reinterpret_cast<const char*>(&R), sizeof(Item));

//avanca o ap para uma pos a dir
    Ei++;

}

void QuickSortExterno::Particao(std::fstream& ArqLi, std::fstream& ArqEi, std::fstream& ArqLEs, PivotBuffer& pivot, int Esq, int Dir, int& i, int& j){
    int Ls = Dir, Es = Dir, Li = Esq, Ei = Esq;// variaveis
    float Linf = -std::numeric_limits<float>::max();   
    float Lsup = std::numeric_limits<float>::max();    
    bool OndeLer = true;
    Item UltLido, R;

    pivot.ClearVector(); //esvaziar por garantia
    i = Esq - 1;
    j = Dir + 1;
    
    while (Ls >= Li) {
    // lendo alternado dos extremos, regidstrossd
        if (pivot.OccupiedCOunt() < TAMAREA - 1) {
            if (OndeLer) LeSup(ArqLEs, UltLido, Ls, OndeLer);
            else         LeInf(ArqLi, UltLido, Li, OndeLer);
            pivot.Insert(UltLido);
            continue;
        }
    
    }

    //garante que a leitura nao alcance a escrita (nao pode perder registro)
        if (Ls == Es)            LeSup(ArqLEs, UltLido, Ls, OndeLer);
        else if (Li == Ei)       LeInf(ArqLi, UltLido, Li, OndeLer);
        else if (OndeLer)        LeSup(ArqLEs, UltLido, Ls, OndeLer);
        else                     LeInf(ArqLi, UltLido, Li, OndeLer);

    //maior que o limite superior: vai direto pra A2
        Metrics::RecordKeyComparison();
        if (UltLido.grade > Lsup) {
            j = Es;
            EscreveMax(ArqLEs, UltLido, Es);
            continue;
        }

    //menor que o limite inferior: vai direto pra A1
        Metrics::RecordKeyComparison();
        if (UltLido.grade < Linf) {
            i = Ei;
            EscreveMin(ArqEi, UltLido, Ei);
            continue;
        }

    //esta entre os limites, sai subarq menor
        pivot.Insert(UltLido);
        if (Ei - Esq < Dir - Es) {
            R = pivot.MinRemove();
            EscreveMin(ArqEi, R, Ei);
            Linf = R.grade;
        } else {
            R = pivot.MaxRemove();
            EscreveMax(ArqLEs, R, Es);
            Lsup = R.grade;
        }
    }

    //descarrega a area no meio do arquivo e ja deixa ordenado
    while (Ei <= Es) {
        R = pivot.MinRemove();
        EscreveMin(ArqEi, R, Ei);
    }

    //os 3 fstream sao o mesmo arquivo: manda pro disco o que foi escrito
    //senao a proxima particao pode ler dado velho do buffer
    ArqEi.flush();
    ArqLEs.flush();

}