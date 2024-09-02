#ifndef DRAM_LAYOUT_H
#define DRAM_LAYOUT_H

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

struct DRAMLayout {
  std::vector<uint64_t> h_fns;
  std::vector<uint64_t> row_masks = { 0 };
  std::vector<uint64_t> col_masks = { 0 };

  DRAMLayout(const std::vector<uint64_t> &h_fns,
             const std::vector<uint64_t> &row_masks,
             const std::vector<uint64_t> &col_masks)
      : h_fns(h_fns), row_masks(row_masks), col_masks(col_masks) {}

  uint64_t get_banks_cnt() const {
    return 1 << h_fns.size();
  }

  std::string str() const;

  operator std::string() const {
    return str();
  }
};

std::ostream &operator<<(std::ostream &out, const DRAMLayout &d_layout);

#endif // DRAM_LAYOUT_H
