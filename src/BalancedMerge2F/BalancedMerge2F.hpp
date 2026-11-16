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

    static uint64_t comparisonCount;

  private:
    struct Slot {
        Item item;
        bool marked;

        bool operator>(const Slot& b) const {
            if (marked != b.marked) {
                return marked;
            }
            return item > b.item;
        }
    };

    MergeConfig cfg_;
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