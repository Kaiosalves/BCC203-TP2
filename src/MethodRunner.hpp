#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "File.hpp"

struct MethodReport {
    std::string title;
    std::vector<std::pair<std::string, uint64_t>> extraMetrics;
    std::string readLabel = "Transferências de leitura:         ";
    std::string writeLabel = "Transferências de escrita:         ";
};

struct SortRunResult {
    double phase1TimeMs = 0.0;
    double phase2TimeMs = 0.0;
    bool valid = true;
    MethodReport report;
};

int RunSortMethod(
    const std::shared_ptr<File> &file,
    const std::function<SortRunResult(const std::shared_ptr<File> &)> &sortFn);
