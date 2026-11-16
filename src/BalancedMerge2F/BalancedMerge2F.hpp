#pragma once

#include <memory>
#include <vector>

#include "File.hpp"
#include "Item.hpp"
#include "Tape.hpp"

struct MergeConfig {
    int memorySize = 10;
    int inputTapes = 10;
    int outputTapes = 10;
};

class BalancedMerge2F {
  public:
    explicit BalancedMerge2F(MergeConfig cfg = {});

    Tape *Sort(File &input);

    double phase1TimeMs() const { return phase1TimeMs_; }
    double phase2TimeMs() const { return phase2TimeMs_; }

    static uint64_t comparisonCount;

  private:
    MergeConfig cfg_;
    double phase1TimeMs_ = 0.0;
    double phase2TimeMs_ = 0.0;
    std::vector<std::unique_ptr<Tape>> tapes_;
    std::vector<Tape *> in_;
    std::vector<Tape *> out_;

    void CreateTapes();
    void GenerateRuns(File &input);
    void MergePass();
    void MergeOneRun(Tape &out);
    static uint64_t TotalRuns(const std::vector<Tape *> &tapes);
    void SwapRoles();
};

int RunMethod1(const std::shared_ptr<File> &file);