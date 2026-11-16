
#pragma once

#include <cstdint>

// Estatísticas do método. Leituras e escritas são contadas por registro.
struct SortStats {
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t comparisons = 0;
    uint64_t runs = 0;
    uint64_t mergePasses = 0;
    double seconds = 0.0;
};
