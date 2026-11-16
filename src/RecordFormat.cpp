#include "RecordFormat.hpp"

#include <cstddef>
#include <cstdio>

namespace {

std::string TrimRight(const char *data, std::size_t size) {
    std::string text(data, size);
    const auto last = text.find_last_not_of(" \0", std::string::npos, 2);
    text.erase(last == std::string::npos ? 0 : last + 1);
    return text;
}

void AppendFixedField(std::string &out, const char *data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(data[i] == '\0' ? ' ' : data[i]);
    }
}

}  // namespace

namespace RecordFormat {

Item ParseLine(std::string line) {
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
    constexpr std::size_t LINE_LEN = DEGREE_POS + DEGREE_LEN;

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.size() < LINE_LEN) {
        line.resize(LINE_LEN, ' ');
    }

    Item reg{};
    reg.registrationNumber = std::stol(line.substr(REG_POS, REG_LEN));
    reg.grade = std::stof(line.substr(GRADE_POS, GRADE_LEN));
    line.copy(reg.state.data(), STATE_LEN, STATE_POS);
    line.copy(reg.city.data(), CITY_LEN, CITY_POS);
    line.copy(reg.degree.data(), DEGREE_LEN, DEGREE_POS);
    return reg;
}

std::string FormatProvaoLine(const Item &item) {
    std::string line;
    line.reserve(99);

    char head[32];
    std::snprintf(head, sizeof(head), "%08ld %5.1f ", item.registrationNumber,
                  static_cast<double>(item.grade));
    line.append(head);

    AppendFixedField(line, item.state.data(), item.state.size());
    line.push_back(' ');
    AppendFixedField(line, item.city.data(), item.city.size());
    line.push_back(' ');
    AppendFixedField(line, item.degree.data(), item.degree.size());

    return line;
}

std::string FormatDisplayLine(const Item &item) {
    char head[64];
    std::snprintf(head, sizeof(head), "%08ld %5.1f %.2s ",
                  item.registrationNumber, static_cast<double>(item.grade),
                  item.state.data());
    return std::string(head) + TrimRight(item.city.data(), item.city.size()) +
           " | " + TrimRight(item.degree.data(), item.degree.size());
}

}  // namespace RecordFormat
