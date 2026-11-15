#include "BalancedMerge2F.hpp"

#include <stdexcept>
#include <queue>
#include <algorithm>

uint64_t BalancedMerge2F::comparisonCount = 0;

BalancedMerge2F::BalancedMerge2F(MergeConfig cfg) : cfg_(cfg) {}

Tape *BalancedMerge2F::Sort(File &input) {
    this->CreateTapes();
    this->GenerateRuns(input);

    while (TotalRuns(this->in_) > 1) {
        this->MergePass();
        this->SwapRoles();
    }

    return this->in_.front();
}

void BalancedMerge2F::CreateTapes() {
    const int total = this->cfg_.inputTapes + this->cfg_.outputTapes;
    this->tapes_.clear();
    for (int i = 0; i < total; i++) {
        this->tapes_.push_back(std::make_unique<Tape>());
    }
    this->in_.clear();
    this->out_.clear();
    for (int i = 0; i < this->cfg_.inputTapes; i++) {
        this->in_.push_back(this->tapes_[i].get());
    }
    for (int i = 0; i < this->cfg_.outputTapes; i++) {
        this->out_.push_back(this->tapes_[this->cfg_.inputTapes + i].get());
    }
}

void BalancedMerge2F::GenerateRuns(File &input) {
    std::priority_queue<Slot, std::vector<Slot>, std::greater<Slot>> mem;

    size_t currentTapeIdx = 0;
    in_[currentTapeIdx]->BeginWrite();

    Item out;
    uint32_t loaded = 0;
    //enchendo memória com itens desmarcados
    while (loaded < static_cast<uint32_t>(cfg_.memorySize) && input.GetNextItem(out)) {
        mem.push(Slot{out, false});
        loaded++;
    }

    while (!mem.empty()) {
        Slot minSlot = mem.top();//menor item do heap
        mem.pop();//remove o item
        //escreve na fita de saída atual
        in_[currentTapeIdx]->WriteItem(minSlot.item);
        
        Item nextItem;//le o próximo item
        if (input.GetNextItem(nextItem)) {
            bool marked = false;
            comparisonCount++;
            // se o item atual for menor que o ultimo entra marcado no heap
            if (nextItem < minSlot.item) {
                marked = true;
            }
            mem.push(Slot{nextItem, marked});
        }
        //se nao leu mais itens a intercalação finaliza
        if (mem.empty()) {
            in_[currentTapeIdx]->EndRun();
            break;
        }
        //verifica se todos os itens sao marcados
        std::vector<Slot> tempHeap;
        bool allMarked = true;
        while (!mem.empty()) {
            Slot s = mem.top();
            mem.pop();
            if (!s.marked) {
                allMarked = false;
            }
            tempHeap.push_back(s);
        }
        for (const auto& s : tempHeap) {
            mem.push(s);
        }
        //se todos estiverem marcados inicia um novo ciclo
        if (allMarked) {
            //registra que o bloco acabou
            in_[currentTapeIdx]->EndRun();
            std::vector<Slot> unmarkedHeap;
            //desmarca todos os itens do heap
            while (!mem.empty()) {
                Slot s = mem.top();
                mem.pop();
                s.marked = false;
                unmarkedHeap.push_back(s);
            }
            for (const auto& s : unmarkedHeap) {
                mem.push(s);
            }
            //pula para a próxima fita
            currentTapeIdx = (currentTapeIdx + 1) % in_.size();
            in_[currentTapeIdx]->BeginWrite();
        }
    }
}

void BalancedMerge2F::MergePass() {
    //reinicia as fitas de entrada e saida para o comeco
    for (Tape *t : in_) {
        t->BeginRead();
    }
    for (Tape *t : out_) {
        t->BeginWrite();
    }

    size_t idxOut = 0;
    while (TotalRuns(in_) > 0) {
        MergeOneRun(*out_[idxOut]);
        out_[idxOut]->EndRun();
        idxOut = (idxOut + 1) % out_.size();
    }
}

void BalancedMerge2F::MergeOneRun(Tape &out) {
    //estrutura para armazenar os primeiros itens das fitas
    struct Head {
        Item item;
        Tape *tape;
        bool active;
    };

    std::vector<Head> heads;
    //armazena o primeiro item das fitas em um vetor
    for (Tape *t : in_) {
        if (t->RunCount() > 0) {
            if (t->StartNextRun()) {
                Item item;
                if (t->ReadItem(item)) {
                    heads.push_back(Head{item, t, true});
                }
            }
        }
    }

    while (true) {
        int minIdx = -1;
        //encontra qual fita tem o menor item
        for (size_t i = 0; i < heads.size(); i++) {
            if (heads[i].active) {
                if (minIdx == -1) {
                    minIdx = i;
                } else {
                    comparisonCount++;
                    if (heads[i].item < heads[minIdx].item) {
                        minIdx = i;
                    }
                }
            }
        }
        //se nenhuma cabeca estiver ativa o run acabou
        if (minIdx == -1) {
            break;
        }
        //escreve o menor o item na fita de saida 
        out.WriteItem(heads[minIdx].item);
        // pega o proximo item da fita que o menor item foi retirada 
        Item nextItem;
        if (heads[minIdx].tape->ReadItem(nextItem)) {
            heads[minIdx].item = nextItem;
        } else {
            heads[minIdx].active = false;
        }
    }
}

uint64_t BalancedMerge2F::TotalRuns(const std::vector<Tape *> &tapes) {
    uint64_t total = 0;
    for (const Tape *t : tapes) {
        total += t->RunCount();
    }
    return total;
}

void BalancedMerge2F::SwapRoles() { 
    this->in_.swap(this->out_);
    //inverte o papel das fitas 
}