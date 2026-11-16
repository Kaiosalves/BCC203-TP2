
#include "ProvaoReader.hpp"

#include <stdexcept>
#include <string>

namespace {

// Posições e tamanhos dos campos (base zero).
constexpr std::size_t REG_POS = 0;
constexpr std::size_t REG_LEN = 8;
constexpr std::size_t GRADE_POS = 9;
constexpr std::size_t GRADE_LEN = 5;
constexpr std::size_t STATE_POS = 15;
constexpr std::size_t STATE_LEN = 2;
constexpr std::size_t CITY_POS = 18;
constexpr std::size_t CITY_LEN = 50;
constexpr std::size_t DEGREE_POS = 69;
constexpr std::size_t DEGREE_LEN = 30;
constexpr std::size_t LINE_LEN = DEGREE_POS + DEGREE_LEN;  // 99 colunas

}  // namespace

ProvaoReader::ProvaoReader(const std::string &path, uint64_t limit)
    : in_(path), path_(path), limit_(limit) {
    if (!this->in_.is_open()) {
        throw std::runtime_error("Não foi possível abrir o arquivo de entrada: " +
                                 path);
    }
}

bool ProvaoReader::Next(Item &out) {
    if (this->delivered_ >= this->limit_) {
        return false;
    }

    while (std::getline(this->in_, this->line_)) {
        ++this->lineNumber_;

        // Remove o '\r' de arquivos com final de linha do Windows.
        if (!this->line_.empty() && this->line_.back() == '\r') {
            this->line_.pop_back();
        }

        // Ignora linhas em branco.
        if (this->line_.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }

        this->ParseLine(out);
        ++this->delivered_;
        return true;
    }

    return false;
}

void ProvaoReader::ParseLine(Item &out) {
    // Completa com espaços se a linha estiver incompleta.
    if (this->line_.size() < LINE_LEN) {
        this->line_.resize(LINE_LEN, ' ');
    }

    try {
        out.registrationNumber =
            std::stol(this->line_.substr(REG_POS, REG_LEN));
        out.grade = std::stof(this->line_.substr(GRADE_POS, GRADE_LEN));
    } catch (const std::exception &) {
        throw std::runtime_error("Linha " + std::to_string(this->lineNumber_) +
                                 " de " + this->path_ +
                                 " fora do formato esperado");
    }

    this->line_.copy(out.state.data(), STATE_LEN, STATE_POS);
    this->line_.copy(out.city.data(), CITY_LEN, CITY_POS);
    this->line_.copy(out.degree.data(), DEGREE_LEN, DEGREE_POS);
}
