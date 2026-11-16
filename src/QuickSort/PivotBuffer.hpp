#pragma once

#include <cstddef>
#include <vector>

#include "Common.hpp"
#include "Item.hpp"

inline constexpr int TAMAREA = 10;

class PivotBuffer {
  private:
    std::vector<Item> items_;

  public:
    // Mantém o vetor interno ordenado por busca binária, evitando reordenar
    // todo o buffer a cada inserção.
    void Insert(const Item &item) {
        std::size_t lo = 0;
        std::size_t hi = items_.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            Metrics::RecordKeyComparison();
            if (items_[mid].grade < item.grade) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(lo), item);
    }
    Item MinRemove(){
        Item min = items_.front();
        items_.erase(items_.begin());
        return min;
    }

    Item MaxRemove(){
        Item max = items_.back();
        items_.pop_back();
        return max;
    }
    
    int OccupiedCount() const {
        return static_cast<int>(items_.size());
    }

    void ClearVector(){
        items_.clear();
    }

};