#include "BalancedMerge2F.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "BinaryIO.hpp"
#include "Common.hpp"
#include "MethodRunner.hpp"

namespace {

template <typename T, typename LessFn>
void SiftDown(std::vector<T> &heap, std::size_t idx, std::size_t heapSize,
              LessFn &&less) {
    while (true) {
        const std::size_t left = 2 * idx + 1;
        if (left >= heapSize) {
            return;
        }
        std::size_t smallest = left;
        if (left + 1 < heapSize && less(heap[left + 1], heap[left])) {
            smallest = left + 1;
        }
        if (!less(heap[smallest], heap[idx])) {
            return;
        }
        std::swap(heap[idx], heap[smallest]);
        idx = smallest;
    }
}

template <typename T, typename LessFn>
void BuildMinHeap(std::vector<T> &heap, std::size_t heapSize, LessFn &&less) {
    for (std::size_t i = heapSize / 2; i > 0; --i) {
        SiftDown(heap, i - 1, heapSize, less);
    }
}

}  // namespace

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

    const std::size_t capacity = static_cast<std::size_t>(cfg_.memorySize);
    if (capacity == 0) {
        return;
    }

    // Heap de mínimo in-place de capacidade M: [0..activeSize-1] ativos e
    // [activeSize..totalSize-1] congelados para o próximo bloco.
    std::vector<Item> heap(capacity);
    std::size_t totalSize = 0;
    while (totalSize < capacity && readInput(heap[totalSize])) {
        ++totalSize;
    }
    if (totalSize == 0) {
        return;
    }

    auto compareGrades = [](const Item &a, const Item &b) {
        ++comparisonCount;
        Metrics::RecordKeyComparison();
        return a.grade < b.grade;
    };

    std::size_t activeSize = totalSize;
    BuildMinHeap(heap, activeSize, compareGrades);

    std::size_t currentTapeIdx = 0;

    while (totalSize > 0) {
        const Item minItem = heap[0];
        in_[currentTapeIdx]->WriteItem(minItem);

        Item nextItem{};
        if (readInput(nextItem)) {
            ++comparisonCount;
            Metrics::RecordKeyComparison();
            if (nextItem.grade >= minItem.grade) {
                heap[0] = nextItem;
                SiftDown(heap, 0, activeSize, compareGrades);
            } else {
                // Congela na cauda reservada e reduz o tamanho ativo do heap.
                heap[0] = heap[activeSize - 1];
                heap[activeSize - 1] = nextItem;
                --activeSize;
                if (activeSize > 0) {
                    SiftDown(heap, 0, activeSize, compareGrades);
                }
            }
        } else {
            // Fim da entrada: remove do heap ativo preservando os congelados.
            heap[0] = heap[activeSize - 1];
            if (activeSize < totalSize) {
                heap[activeSize - 1] = heap[totalSize - 1];
            }
            --activeSize;
            --totalSize;
            if (activeSize > 0) {
                SiftDown(heap, 0, activeSize, compareGrades);
            }
        }

        if (activeSize == 0) {
            in_[currentTapeIdx]->EndRun();
            if (totalSize == 0) {
                break;
            }
            activeSize = totalSize;
            BuildMinHeap(heap, activeSize, compareGrades);
            currentTapeIdx = (currentTapeIdx + 1) % in_.size();
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
    struct Head {
        Item item;
        Tape *tape;
    };

    std::vector<Head> heap;
    heap.reserve(in_.size());
    for (Tape *t : in_) {
        if (t->RunCount() > 0 && t->StartNextRun()) {
            Item item{};
            if (t->ReadItem(item)) {
                heap.push_back(Head{item, t});
            }
        }
    }

    if (heap.empty()) {
        return;
    }

    auto compareHeads = [](const Head &a, const Head &b) {
        ++comparisonCount;
        Metrics::RecordKeyComparison();
        return a.item.grade < b.item.grade;
    };

    std::size_t heapSize = heap.size();
    BuildMinHeap(heap, heapSize, compareHeads);

    while (heapSize > 0) {
        out.WriteItem(heap[0].item);

        Item nextItem{};
        if (heap[0].tape->ReadItem(nextItem)) {
            heap[0].item = nextItem;
            SiftDown(heap, 0, heapSize, compareHeads);
        } else {
            heap[0] = heap[--heapSize];
            if (heapSize > 0) {
                SiftDown(heap, 0, heapSize, compareHeads);
            }
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