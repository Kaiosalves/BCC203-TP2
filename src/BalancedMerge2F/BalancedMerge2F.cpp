#include "BalancedMerge2F.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <queue>
#include <stdexcept>

#include "BinaryIO.hpp"
#include "Common.hpp"
#include "MethodRunner.hpp"

uint64_t BalancedMerge2F::comparisonCount = 0;

BalancedMerge2F::BalancedMerge2F(MergeConfig cfg) : cfg_(cfg) {}

Tape *BalancedMerge2F::Sort(File &input) {
    this->CreateTapes();

    Metrics::SetPhase(Phase::Preprocessing);
    const auto p1Start = std::chrono::steady_clock::now();
    this->GenerateRuns(input);
    const auto p1End = std::chrono::steady_clock::now();
    this->phase1TimeMs_ =
        std::chrono::duration<double, std::milli>(p1End - p1Start).count();

    Metrics::SetPhase(Phase::Search);
    const auto p2Start = std::chrono::steady_clock::now();
    while (TotalRuns(this->in_) > 1) {
        this->MergePass();
        this->SwapRoles();
    }
    const auto p2End = std::chrono::steady_clock::now();
    this->phase2TimeMs_ =
        std::chrono::duration<double, std::milli>(p2End - p2Start).count();

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
    struct MetricsScope {
        File &file;
        ~MetricsScope() { file.SetRecordMetrics(false); }
    };
    input.SetRecordMetrics(true);
    const MetricsScope metricsGuard{input};
    input.Rewind();

    std::array<Item, PAGE_SIZE> page{};
    uint64_t pageIdx = 0;
    uint64_t pageCount = 0;
    uint64_t consumed = 0;

    auto readInput = [&](Item &item) {
        if (consumed >= input.quantity()) {
            return false;
        }
        if (pageIdx >= pageCount) {
            if (input.eof()) {
                return false;
            }
            page = input.GetNextPage();
            pageCount = input.lastPageCount();
            pageIdx = 0;
            if (pageCount == 0) {
                return false;
            }
        }
        item = page[pageIdx++];
        ++consumed;
        return true;
    };

    for (Tape *t : in_) {
        t->BeginWrite();
    }

    std::priority_queue<Slot, std::vector<Slot>, std::greater<Slot>> mem;

    size_t currentTapeIdx = 0;

    Item out;
    uint32_t loaded = 0;
    // enchendo memória com itens desmarcados
    while (loaded < static_cast<uint32_t>(cfg_.memorySize) && readInput(out)) {
        mem.push(Slot{out, false});
        loaded++;
    }

    while (!mem.empty()) {
        Slot minSlot = mem.top(); // menor item do heap
        mem.pop();                // remove o item
        // escreve na fita de saída atual
        in_[currentTapeIdx]->WriteItem(minSlot.item);

        Item nextItem; // le o próximo item
        if (readInput(nextItem)) {
            bool marked = false;
            comparisonCount++;
            Metrics::RecordKeyComparison();
            // se o item atual for menor que o ultimo entra marcado no heap
            if (nextItem < minSlot.item) {
                marked = true;
            }
            mem.push(Slot{nextItem, marked});
        }
        // se nao leu mais itens a intercalação finaliza
        if (mem.empty()) {
            in_[currentTapeIdx]->EndRun();
            break;
        }
        // verifica se todos os itens sao marcados
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
        // se todos estiverem marcados inicia um novo ciclo
        if (allMarked) {
            // registra que o bloco acabou
            in_[currentTapeIdx]->EndRun();
            // desmarca todos os itens do heap
            for (auto& s : tempHeap) {
                s.marked = false;
            }
            // pula para a próxima fita
            currentTapeIdx = (currentTapeIdx + 1) % in_.size();
        }
        for (const auto& s : tempHeap) {
            mem.push(s);
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
                    Metrics::RecordKeyComparison();
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

int RunMethod1(const std::shared_ptr<File> &file) {
    return RunSortMethod(file, [](const std::shared_ptr<File> &inputFile) {
        Metrics::Reset();
        BalancedMerge2F::comparisonCount = 0;

        BalancedMerge2F sorter;
        Tape *resultTape = sorter.Sort(*inputFile);

        if (resultTape != nullptr) {
            resultTape->SetRecordMetrics(false);
            inputFile->CloseStream();
            std::ofstream out(inputFile->binaryPath(),
                              std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                throw std::runtime_error(
                    "Não foi possível abrir o arquivo binário para escrita");
            }
            resultTape->BeginRead();
            std::array<Item, PAGE_SIZE> page{};
            std::size_t count = 0;
            while (resultTape->StartNextRun()) {
                Item item;
                while (resultTape->ReadItem(item)) {
                    page[count++] = item;
                    if (count == PAGE_SIZE) {
                        BinaryIO::WriteItemsPage(
                            out, page, count,
                            "Erro ao escrever arquivo binário ordenado");
                        count = 0;
                    }
                }
            }
            if (count > 0) {
                BinaryIO::WriteItemsPage(
                    out, page, count,
                    "Erro ao escrever arquivo binário ordenado");
            }
            out.flush();
            out.close();
            resultTape->SetRecordMetrics(true);
            inputFile->ReopenStream();
        }

        SortRunResult result;
        result.phase1TimeMs = sorter.phase1TimeMs();
        result.phase2TimeMs = sorter.phase2TimeMs();
        result.valid = (resultTape != nullptr);
        result.report.title =
            "=== Método 1: intercalação balanceada 2f fitas ===";
        result.report.readLabel = "Transferências de leitura (blocos):";
        result.report.writeLabel = "Transferências de escrita (blocos):";
        return result;
    });
}