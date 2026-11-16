#include "MethodRunner.hpp"

#include <cstdio>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Common.hpp"

int RunSortMethod(
    const std::shared_ptr<File> &file,
    const std::function<SortRunResult(const std::shared_ptr<File> &)> &sortFn) {
    try {
        if (file == nullptr) {
            throw std::invalid_argument("Arquivo de entrada inválido");
        }

        const bool print = Log::enableInfo;
        if (print) {
            file->PrintContents();
        }

        const SortRunResult result = sortFn(file);

        const PhaseMetrics phase1 = Metrics::Preprocessing();
        const PhaseMetrics phase2 = Metrics::Search();

        if (!result.valid || !file->VerifySorted(print)) {
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
            (result.phase1TimeMs + result.phase2TimeMs) / 1000.0;

        char seconds[32];
        std::snprintf(seconds, sizeof(seconds), "%.6f", totalSeconds);

        std::cout << result.report.title << '\n'
                  << "Registros ordenados:               " << file->quantity()
                  << '\n';
        for (const auto &[label, value] : result.report.extraMetrics) {
            std::cout << label << value << '\n';
        }
        std::cout << result.report.readLabel << totalReads << '\n'
                  << result.report.writeLabel << totalWrites << '\n'
                  << "Comparações entre chaves:          " << totalComparisons
                  << '\n'
                  << "Tempo de execução (s):             " << seconds << '\n'
                  << "{\"phase1_time_ms\":" << result.phase1TimeMs
                  << ",\"phase1_reads\":" << phase1.diskReads
                  << ",\"phase1_writes\":" << phase1.diskWrites
                  << ",\"phase2_time_ms\":" << result.phase2TimeMs
                  << ",\"phase2_reads\":" << phase2.diskReads
                  << ",\"comparisons\":" << totalComparisons << "}\n";
        return 0;
    } catch (const std::exception &e) {
        Log::Error(e.what());
        return -1;
    }
}
