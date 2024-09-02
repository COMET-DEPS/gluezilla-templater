#ifndef CONTIGUOUS_FLIP_FINDER_H
#define CONTIGUOUS_FLIP_FINDER_H

#include <cstdint>
#include <map>
#include <vector>

#include "bit_flipper.h"
#include "config.h"
#include "dram_address.h"
#include "flip_finder.h"
#include "phys_page_finder.h"

class ContiguousFlipFinder : public FlipFinder {

  const uint32_t hammer_pages;

  uint64_t pages_per_bank;
  std::map<bank_t, std::vector<physaddr_t>> banks;

  // find first run of allocated pages
  static bool find_run(const PhysPageFinder &finder, page_t &first_page,
                       page_t &last_page, uint64_t min_length);
  static bool find_run_fixed(const PhysPageFinder &finder, page_t first_page,
                             page_t &last_page, uint64_t min_length);

  // returns the starting address of the first page in a row
  static physaddr_t find_first_page_in_row(row_t row);
  bool determine_page_range(page_t &first_page, page_t &last_page);
  void default_test() const;
  void fast_test() const;
  void debug_test() const;
  bool hammer(bank_t bank,
              const std::vector<page_t>::const_iterator &pages_begin,
              const std::vector<page_t>::const_iterator &pages_end) const;

public:
  ContiguousFlipFinder(const PhysPageFinder &finder)
      : FlipFinder(finder), hammer_pages(hammer_rows * pages_per_row) {}

  void find_flips() override;
};

#endif // CONTIGUOUS_FLIP_FINDER_H
