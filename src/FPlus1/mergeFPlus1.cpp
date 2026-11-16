#include "MergeFPlus1.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "Common.hpp"
#include "File.hpp"
#include "Item.hpp"
#include "Tape.hpp"

BalancedMergeFPlus1::BufferedTape::BufferedTape()
    : tape(std::make_unique<Tape>()) {}

void BalancedMergeFPlus1::BufferedTape::Clear() {
    this->tape->Clear();
    this->runs.clear();
    this->bufPos = 0;
    this->currentRunLength = 0;
    this->totalRecords = 0;
    this->recordsRead = 0;
    this->nextRun = 0;
    this->reading = false;
}

void BalancedMergeFPlus1::BufferedTape::WriteRecord(const Item &item) {
    this->buffer[this->bufPos++] = item;
    ++this->currentRunLength;
    ++this->totalRecords;

    if (this->bufPos == PAGE_SIZE) {
        this->tape->WriteBlock(this->buffer);
        this->bufPos = 0;
    }
}

void BalancedMergeFPlus1::BufferedTape::EndRun() {
    if (this->currentRunLength > 0) {
        this->runs.push_back(this->currentRunLength);
        this->currentRunLength = 0;
    }
}

void BalancedMergeFPlus1::BufferedTape::Rewind() {
    this->EndRun();

    if (!this->reading && this->bufPos > 0) {
        this->tape->WriteBlock(this->buffer);
        this->bufPos = 0;
    }

    this->tape->Rewind();
    this->bufPos = PAGE_SIZE;
    this->recordsRead = 0;
    this->nextRun = 0;
    this->reading = true;
}

bool BalancedMergeFPlus1::BufferedTape::ReadRecord(Item &item) {
    if (this->recordsRead >= this->totalRecords) {
        return false;
    }

    if (this->bufPos >= PAGE_SIZE) {
        this->buffer = this->tape->ReadBlock();
        this->bufPos = 0;
    }

    item = this->buffer[this->bufPos++];
    ++this->recordsRead;
    return true;
}

bool BalancedMergeFPlus1::BufferedTape::HasNextRun() const {
    return this->nextRun < this->runs.size();
}

uint64_t BalancedMergeFPlus1::BufferedTape::BeginNextRun() {
    return this->runs[this->nextRun++];
}

std::size_t BalancedMergeFPlus1::BufferedTape::NumRuns() const {
    return this->runs.size();
}

BalancedMergeFPlus1::BalancedMergeFPlus1(std::shared_ptr<File> file)
    : file_(std::move(file)) {
    if (this->file_ == nullptr) {
        throw std::invalid_argument("Arquivo de entrada inválido");
    }

    for (std::size_t i = 0; i < kTotalTapes; ++i) {
        this->tapes_.push_back(std::make_unique<BufferedTape>());
    }
}

void BalancedMergeFPlus1::ReadOrThrow(BufferedTape &tape, Item &item) {
    if (!tape.ReadRecord(item)) {
        throw std::runtime_error(
            "Fim inesperado de fita durante a intercalação");
    }
}

bool BalancedMergeFPlus1::AnyInputHasRun() const {
    for (std::size_t t = 0; t < kInputTapes; ++t) {
        if (this->tapes_[t]->HasNextRun()) {
            return true;
        }
    }
    return false;
}

// Gera blocos ordenados por seleção por substituição.
void BalancedMergeFPlus1::GenerateRuns() {
    struct MetricsScope {
        File &file;
        ~MetricsScope() { file.SetRecordMetrics(false); }
    };
    this->file_->SetRecordMetrics(true);
    const MetricsScope metricsGuard{*this->file_};

    this->file_->Rewind();

    std::array<Item, PAGE_SIZE> page{};
    uint64_t pageIdx = 0;
    uint64_t pageCount = 0;
    uint64_t consumed = 0;

    auto readInput = [&](Item &item) {
        if (consumed >= this->file_->quantity()) {
            return false;
        }
        if (pageIdx >= pageCount) {
            if (this->file_->eof()) {
                return false;
            }
            page = this->file_->GetNextPage();
            pageCount = this->file_->lastPageCount();
            pageIdx = 0;
            if (pageCount == 0) {
                return false;
            }
        }
        item = page[pageIdx++];
        ++consumed;
        return true;
    };

    // Carrega a memória inicial.
    this->heap_.Clear();
    while (!this->heap_.Full()) {
        FMais1::HeapEntry &slot = this->heap_.Append();
        if (!readInput(slot.item)) {
            this->heap_.DropLast();
            break;
        }
    }
    if (this->heap_.Empty()) {
        throw std::runtime_error("O arquivo de entrada não possui registros");
    }
    this->heap_.Build();

    std::size_t tapeIndex = 0;  // fita que recebe o bloco corrente
    uint32_t currentRun = 0;    // número do bloco corrente
    uint64_t runCount = 1;

    while (!this->heap_.Empty()) {
        FMais1::HeapEntry &root = this->heap_.Root();

        // Ao mudar de bloco, avança para a próxima fita.
        if (root.run != currentRun) {
            this->tapes_[tapeIndex]->EndRun();
            tapeIndex = (tapeIndex + 1) % kInputTapes;
            currentRun = root.run;
            ++runCount;
        }

        this->tapes_[tapeIndex]->WriteRecord(root.item);

        const float lastKey = root.item.grade;
        const uint32_t lastRun = root.run;

        if (readInput(root.item)) {
            // Uma comparação de chaves: o novo registro ainda cabe no bloco?
            Metrics::RecordKeyComparison();
            root.run = (root.item.grade < lastKey) ? lastRun + 1 : lastRun;
            this->heap_.FixRoot();
        } else {
            this->heap_.PopRoot();
        }
    }
    this->tapes_[tapeIndex]->EndRun();
    this->runs_ = runCount;

    if (consumed < this->file_->quantity()) {
        throw std::runtime_error(
            "O arquivo de entrada possui apenas " + std::to_string(consumed) +
            " registros, mas foram pedidos " +
            std::to_string(this->file_->quantity()));
    }
}

// Intercala uma rodada das fitas de entrada na fita de saída.
void BalancedMergeFPlus1::MergePass() {
    BufferedTape &out = *this->tapes_[kInputTapes];
    out.Clear();
    for (std::size_t t = 0; t < kInputTapes; ++t) {
        this->tapes_[t]->Rewind();
    }

    // Registros restantes no bloco atual de cada fita.
    std::array<uint64_t, kInputTapes> remaining{};

    while (this->AnyInputHasRun()) {
        this->heap_.Clear();

        // Carrega o primeiro registro de cada bloco da rodada.
        for (std::size_t t = 0; t < kInputTapes; ++t) {
            BufferedTape &in = *this->tapes_[t];
            if (!in.HasNextRun()) {
                continue;
            }

            remaining[t] = in.BeginNextRun();
            FMais1::HeapEntry &slot = this->heap_.Append();
            slot.tape = static_cast<uint32_t>(t);
            ReadOrThrow(in, slot.item);
            --remaining[t];
        }
        this->heap_.Build();

        // Grava o menor registro e repõe pela mesma fita.
        while (!this->heap_.Empty()) {
            FMais1::HeapEntry &root = this->heap_.Root();
            out.WriteRecord(root.item);

            const uint32_t t = root.tape;
            if (remaining[t] > 0) {
                ReadOrThrow(*this->tapes_[t], root.item);
                --remaining[t];
                this->heap_.FixRoot();
            } else {
                this->heap_.PopRoot();
            }
        }
        out.EndRun();
    }

    // As fitas de entrada foram totalmente consumidas.
    for (std::size_t t = 0; t < kInputTapes; ++t) {
        this->tapes_[t]->Clear();
    }
}

// Redistribui os blocos de saída entre as fitas de entrada.
void BalancedMergeFPlus1::Redistribute() {
    BufferedTape &out = *this->tapes_[kInputTapes];
    out.Rewind();

    Item record{};
    const std::size_t runCount = out.NumRuns();
    for (std::size_t j = 0; j < runCount; ++j) {
        const uint64_t length = out.BeginNextRun();
        BufferedTape &target = *this->tapes_[j % kInputTapes];

        for (uint64_t k = 0; k < length; ++k) {
            ReadOrThrow(out, record);
            target.WriteRecord(record);
        }
        target.EndRun();
    }
}

void BalancedMergeFPlus1::Sort() {
    Metrics::Reset();
    Metrics::SetPhase(Phase::Preprocessing);

    const auto p1Start = std::chrono::steady_clock::now();
    this->GenerateRuns();
    const auto p1End = std::chrono::steady_clock::now();
    this->phase1TimeMs_ =
        std::chrono::duration<double, std::milli>(p1End - p1Start).count();

    Log::Info("Blocos gerados pela seleção por substituição: " +
              std::to_string(this->runs_));

    Metrics::SetPhase(Phase::Search);
    const auto p2Start = std::chrono::steady_clock::now();
    if (this->runs_ <= 1) {
        // Um único bloco já está ordenado.
        this->result_ = this->tapes_[0].get();
    } else {
        while (true) {
            this->MergePass();
            ++this->mergePasses_;

            BufferedTape &out = *this->tapes_[kInputTapes];
            Log::Info("Passo " + std::to_string(this->mergePasses_) +
                      " de intercalação: " + std::to_string(out.NumRuns()) +
                      " bloco(s) na fita de saída");

            if (out.NumRuns() == 1) {
                this->result_ = &out;
                break;
            }
            this->Redistribute();
        }
    }
    const auto p2End = std::chrono::steady_clock::now();
    this->phase2TimeMs_ =
        std::chrono::duration<double, std::milli>(p2End - p2Start).count();

    this->WriteResultToBinary();
}

void BalancedMergeFPlus1::WriteResultToBinary() {
    if (this->result_ == nullptr) {
        return;
    }

    this->result_->tape->SetRecordMetrics(false);
    this->result_->Rewind();

    this->file_->CloseStream();
    std::ofstream out(this->file_->binaryPath(),
                      std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Falha ao abrir arquivo binário intermediário para escrita: " +
            this->file_->binaryPath());
    }

    std::array<Item, PAGE_SIZE> buffer{};
    std::size_t bufPos = 0;
    Item item{};

    while (this->result_->ReadRecord(item)) {
        buffer[bufPos++] = item;
        if (bufPos == PAGE_SIZE) {
            out.write(reinterpret_cast<const char *>(buffer.data()),
                      static_cast<std::streamsize>(bufPos * sizeof(Item)));
            if (!out) {
                throw std::runtime_error(
                    "Falha ao gravar resultado ordenado no arquivo binário");
            }
            bufPos = 0;
        }
    }

    if (bufPos > 0) {
        out.write(reinterpret_cast<const char *>(buffer.data()),
                  static_cast<std::streamsize>(bufPos * sizeof(Item)));
        if (!out) {
            throw std::runtime_error(
                "Falha ao gravar resultado ordenado no arquivo binário");
        }
    }

    out.flush();
    if (!out) {
        throw std::runtime_error(
            "Falha ao finalizar gravação do arquivo binário intermediário");
    }
    out.close();

    this->result_->tape->SetRecordMetrics(true);
    this->file_->ReopenStream();
}

bool BalancedMergeFPlus1::CheckAndShowResult(bool print) {
    if (this->result_ == nullptr) {
        return false;
    }
    return this->file_->VerifySorted(print);
}

int RunMethod2(const std::shared_ptr<File> &file) {
    try {
        if (file == nullptr) {
            throw std::invalid_argument("Arquivo de entrada inválido");
        }

        const bool print = Log::enableInfo;  // opção -P

        if (print) {
            file->PrintContents();
        }

        BalancedMergeFPlus1 sorter(file);
        sorter.Sort();

        const PhaseMetrics phase1 = Metrics::Preprocessing();
        const PhaseMetrics phase2 = Metrics::Search();

        const bool ok = sorter.CheckAndShowResult(print);
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
        const double totalSeconds =
            (sorter.phase1TimeMs() + sorter.phase2TimeMs()) / 1000.0;

        char seconds[32];
        std::snprintf(seconds, sizeof(seconds), "%.6f", totalSeconds);

        std::cout << "=== Método 2: intercalação balanceada f+1 fitas "
                     "(seleção por substituição) ===\n"
                  << "Registros ordenados:               " << file->quantity()
                  << '\n'
                  << "Blocos gerados:                    " << sorter.runs()
                  << '\n'
                  << "Passos de intercalação:            "
                  << sorter.mergePasses() << '\n'
                  << "Transferências de leitura (blocos):" << totalReads << '\n'
                  << "Transferências de escrita (blocos):" << totalWrites << '\n'
                  << "Comparações entre chaves:          " << totalComparisons
                  << '\n'
                  << "Tempo de execução (s):             " << seconds << '\n'
                  << "{\"phase1_time_ms\":" << sorter.phase1TimeMs()
                  << ",\"phase1_reads\":" << phase1.diskReads
                  << ",\"phase1_writes\":" << phase1.diskWrites
                  << ",\"phase2_time_ms\":" << sorter.phase2TimeMs()
                  << ",\"phase2_reads\":" << phase2.diskReads
                  << ",\"comparisons\":" << totalComparisons << "}\n";
        return 0;
    } catch (const std::exception &e) {
        Log::Error(e.what());
        return -1;
    }
}
