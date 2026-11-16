
#include "MergeFPlus1.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "Common.hpp"
#include "Item.hpp"
#include "ProvaoReader.hpp"

namespace {

// Remove espaços no fim do campo.
std::string TrimRight(const char *data, std::size_t size) {
    std::string text(data, size);
    const auto last = text.find_last_not_of(" \0", std::string::npos, 2);
    text.erase(last == std::string::npos ? 0 : last + 1);
    return text;
}

// Formata um registro para exibição.
std::string FormatItem(const Item &item) {
    char head[64];
    std::snprintf(head, sizeof(head), "%08ld %5.1f %.2s ",
                  item.registrationNumber, static_cast<double>(item.grade),
                  item.state.data());
    return std::string(head) + TrimRight(item.city.data(), item.city.size()) +
           " | " + TrimRight(item.degree.data(), item.degree.size());
}

// Mostra os registros de entrada sem alterar as estatísticas.
void PrintInput(const std::string &path, uint64_t quantity) {
    ProvaoReader reader(path, quantity);
    Item item{};

    std::cout << "--- Dados a serem ordenados (" << quantity
              << " registros) ---\n";
    while (reader.Next(item)) {
        std::cout << FormatItem(item) << '\n';
    }
    std::cout << std::flush;
}

}  // namespace

BalancedMergeFPlus1::BalancedMergeFPlus1(std::string inputPath,
                                         uint64_t quantity)
    : inputPath_(std::move(inputPath)), quantity_(quantity), heap_(stats_) {
    // Cria as fitas temporárias junto ao arquivo de entrada.
    std::filesystem::path dir = std::filesystem::path(this->inputPath_).parent_path();
    if (dir.empty()) {
        dir = ".";
    }

    // Evita colisões entre execuções simultâneas.
    const std::string tag = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());

    for (std::size_t i = 0; i < kTotalTapes; ++i) {
        this->tapes_.push_back(std::make_unique<RunTape>(
            dir / ("fita_m2_" + tag + "_" + std::to_string(i) + ".bin"),
            &this->stats_));
    }
}

void BalancedMergeFPlus1::ReadOrThrow(RunTape &tape, Item &item) {
    if (!tape.ReadRecord(item)) {
        throw std::runtime_error("Fim inesperado de fita durante a intercalação");
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
    ProvaoReader input(this->inputPath_, this->quantity_);
    uint64_t consumed = 0;

    // Lê um registro e atualiza as estatísticas.
    auto readInput = [&](Item &item) {
        if (!input.Next(item)) {
            return false;
        }
        ++this->stats_.reads;
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
            ++this->stats_.comparisons;
            root.run = (root.item.grade < lastKey) ? lastRun + 1 : lastRun;
            this->heap_.FixRoot();
        } else {
            this->heap_.PopRoot();
        }
    }
    this->tapes_[tapeIndex]->EndRun();
    this->stats_.runs = runCount;

    if (consumed < this->quantity_) {
        throw std::runtime_error(
            "O arquivo de entrada possui apenas " + std::to_string(consumed) +
            " registros, mas foram pedidos " + std::to_string(this->quantity_));
    }
}

// Intercala uma rodada das fitas de entrada na fita de saída.
void BalancedMergeFPlus1::MergePass() {
    RunTape &out = *this->tapes_[kInputTapes];
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
            RunTape &in = *this->tapes_[t];
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
    RunTape &out = *this->tapes_[kInputTapes];
    out.Rewind();

    Item record{};
    const std::size_t runCount = out.NumRuns();
    for (std::size_t j = 0; j < runCount; ++j) {
        const uint64_t length = out.BeginNextRun();
        RunTape &target = *this->tapes_[j % kInputTapes];

        for (uint64_t k = 0; k < length; ++k) {
            ReadOrThrow(out, record);
            target.WriteRecord(record);
        }
        target.EndRun();
    }
}

void BalancedMergeFPlus1::Sort() {
    const auto start = std::chrono::steady_clock::now();

    this->GenerateRuns();
    Log::Info("Blocos gerados pela seleção por substituição: " +
              std::to_string(this->stats_.runs));

    if (this->stats_.runs <= 1) {
            // Um único bloco já está ordenado.
        this->result_ = this->tapes_[0].get();
    } else {
        while (true) {
            this->MergePass();
            ++this->stats_.mergePasses;

            RunTape &out = *this->tapes_[kInputTapes];
            Log::Info("Passo " + std::to_string(this->stats_.mergePasses) +
                      " de intercalação: " + std::to_string(out.NumRuns()) +
                      " bloco(s) na fita de saída");

            if (out.NumRuns() == 1) {
                this->result_ = &out;
                break;
            }
            this->Redistribute();
        }
    }

    const auto end = std::chrono::steady_clock::now();
    this->stats_.seconds = std::chrono::duration<double>(end - start).count();
}

bool BalancedMergeFPlus1::CheckAndShowResult(bool print) {
    if (this->result_ == nullptr) {
        return false;
    }

    // A leitura de conferência não entra nas estatísticas.
    this->result_->SetStats(nullptr);
    this->result_->Rewind();

    if (print) {
        std::cout << "--- Resultado da ordenação ---\n";
    }

    Item item{};
    Item previous{};
    uint64_t count = 0;
    bool ordered = true;

    while (this->result_->ReadRecord(item)) {
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

    return ordered && count == this->quantity_;
}

int RunMethod2(const std::string &inputPath, uint64_t quantity) {
    try {
        const bool print = Log::enableInfo;  // opção -P

        if (print) {
            PrintInput(inputPath, quantity);
        }

        BalancedMergeFPlus1 sorter(inputPath, quantity);
        sorter.Sort();

        const bool ok = sorter.CheckAndShowResult(print);
        if (!ok) {
            Log::Error("A verificação do resultado falhou: o arquivo não "
                       "ficou ordenado corretamente");
            return -1;
        }

        const SortStats &s = sorter.stats();
        char seconds[32];
        std::snprintf(seconds, sizeof(seconds), "%.6f", s.seconds);

        std::cout << "=== Método 2: intercalação balanceada f+1 fitas "
                     "(seleção por substituição) ===\n"
                  << "Registros ordenados:               " << quantity << '\n'
                  << "Blocos gerados:                    " << s.runs << '\n'
                  << "Passos de intercalação:            " << s.mergePasses << '\n'
                  << "Transferências de leitura:         " << s.reads << '\n'
                  << "Transferências de escrita:         " << s.writes << '\n'
                  << "Comparações entre chaves:          " << s.comparisons << '\n'
                  << "Tempo de execução (s):             " << seconds << '\n';
        return 0;
    } catch (const std::exception &e) {
        Log::Error(e.what());
        return -1;
    }
}
