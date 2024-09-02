#ifndef NONCONTIGUOUS_FLIP_FINDER_H
#define NONCONTIGUOUS_FLIP_FINDER_H

#include <cstdint>
#include <map>
#include <set>
#include <utility>

#include "bit_flipper.h"
#include "config.h"
#include "dram_address.h"
#include "flip_finder.h"
#include "phys_page_finder.h"

class NoncontiguousFlipFinder : public FlipFinder {

  std::map<bank_t, std::set<row_t>> missing_rows;

  std::pair<page_t, page_t> get_page_bounds() const;
  std::pair<row_t, row_t> get_row_bounds(bank_t bank, page_t first_page,
                                         page_t last_page) const;

  void find_missing_rows(page_t first_page, page_t last_page);
  bool is_any_row_missing(bank_t bank, row_t first_row, row_t last_row) const;
  bool hammer(bank_t bank, row_t first_victim, row_t last_victim) const;

  bool default_test(bank_t bank, row_t first_row, row_t last_row) const;
  bool fast_test(bank_t bank, row_t first_row, row_t last_row) const;
  bool debug_test(bank_t bank, row_t first_row, row_t last_row) const;

public:
  NoncontiguousFlipFinder(const PhysPageFinder &finder)
      : FlipFinder(finder) {}

  void find_flips() override;
};

#endif // NONCONTIGUOUS_FLIP_FINDER_H
