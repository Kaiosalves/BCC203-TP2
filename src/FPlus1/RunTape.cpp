
#include "RunTape.hpp"

#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

// A fita grava os registros como bytes.
static_assert(std::is_trivially_copyable_v<Item>,
              "Item precisa ser trivialmente copiável para ir à fita");

RunTape::RunTape(std::filesystem::path path, SortStats *stats)
    : path_(std::move(path)), stats_(stats) {
    this->Clear();
}

RunTape::~RunTape() {
    if (this->file_.is_open()) {
        this->file_.close();
    }

    // Remove o arquivo temporário.
    std::error_code ec;
    std::filesystem::remove(this->path_, ec);
}

void RunTape::Clear() {
    if (this->file_.is_open()) {
        this->file_.close();
    }
    this->file_.clear();

    // Abre a fita para leitura e escrita, descartando o conteúdo anterior.
    this->file_.open(this->path_, std::ios::in | std::ios::out |
                                      std::ios::binary | std::ios::trunc);
    if (!this->file_.is_open()) {
        throw std::runtime_error("Não foi possível criar a fita temporária: " +
                                 this->path_.string());
    }

    this->runs_.clear();
    this->currentRunLength_ = 0;
    this->nextRun_ = 0;
}

void RunTape::WriteRecord(const Item &item) {
    this->file_.write(reinterpret_cast<const char *>(&item), sizeof(Item));
    if (!this->file_) {
        throw std::runtime_error("Falha de escrita na fita: " +
                                 this->path_.string());
    }

    ++this->currentRunLength_;
    if (this->stats_ != nullptr) {
        ++this->stats_->writes;
    }
}

void RunTape::EndRun() {
    if (this->currentRunLength_ > 0) {
        this->runs_.push_back(this->currentRunLength_);
        this->currentRunLength_ = 0;
    }
}

void RunTape::Rewind() {
    // Fecha o último bloco, se houver.
    this->EndRun();

    // Prepara o fluxo para leitura desde o início.
    this->file_.flush();
    this->file_.clear();
    this->file_.seekg(0, std::ios::beg);
    this->nextRun_ = 0;
}

bool RunTape::ReadRecord(Item &item) {
    this->file_.read(reinterpret_cast<char *>(&item), sizeof(Item));
    if (this->file_.gcount() != static_cast<std::streamsize>(sizeof(Item))) {
        return false;
    }

    if (this->stats_ != nullptr) {
        ++this->stats_->reads;
    }
    return true;
}

bool RunTape::HasNextRun() const { return this->nextRun_ < this->runs_.size(); }

uint64_t RunTape::BeginNextRun() { return this->runs_[this->nextRun_++]; }

std::size_t RunTape::NumRuns() const { return this->runs_.size(); }
