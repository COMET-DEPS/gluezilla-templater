/**
 * @file     contiguous_flip_finder.cpp
 *
 * @brief    Contains the contiguoues flip finder used for hammering over a
 *           contigous memory space.
 *
 */

#include <algorithm>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <functional>
#include <iostream>
#include <iterator>
#include <set>
#include <thread>

#include "db.h"
#include "logging.h"

#include "contiguous_flip_finder.h"

/**
 * @brief    Finds the first hammering run of allocated pages.
 *           Executed when first_row=0.
 *
 * @param    finder              The physical page finder to use.
 * @param    first_page          First possible page of run.
 * @param    last_page           Last possible page of run.
 * @param    min_length          Minimum length of run.
 * @return   true                If run was successfully found.
 * @return   false               If run with minimum length could not be found.
 */
bool ContiguousFlipFinder::find_run(const PhysPageFinder &finder,
                                    page_t &first_page, page_t &last_page,
                                    uint64_t min_length) {
  page_t prev_page = UINT64_MAX; // UINT64_MAX + 1 == 0

  for (const auto &[page, _] : finder) {
    if (page != prev_page + 1) {
      first_page = page;
    } else {
      last_page = page;
      if (last_page > first_page && last_page - first_page > min_length)
        return true;
    }
    prev_page = page;
  }

  return false;
}

/**
 * @brief    Finds the hammering run with a specified first row.
 *
 * @param    finder              The physical page finder to use.
 * @param    first_page          Page that the run should start.
 * @param    last_page           Last possible page of run.
 * @param    min_length          Minimum length of run.
 * @return   true                If run was successfully found.
 * @return   false               If run with minimum length could not be found.
 */
bool ContiguousFlipFinder::find_run_fixed(const PhysPageFinder &finder,
                                          page_t first_page, page_t &last_page,
                                          uint64_t min_length) {
  last_page = first_page;

  for (page_t page = first_page; page <= finder.back().first; ++page) {
    if (finder.contains(page))
      last_page = page;
    else
      break;
  }

  return last_page - first_page > min_length;
}

/**
 * @brief    Finds the first page in a row.
 *
 * @param    row                 The row to determine the first page for.
 * @return   physaddr_t          The starting address of the first page of the
 *                               row.
 */
physaddr_t ContiguousFlipFinder::find_first_page_in_row(row_t row) {
  DRAMAddr dram(0, row, 0);
  physaddr_t min_phys = UINT64_MAX;

  for (uint64_t bank = 0; bank < config.dram_layout.get_banks_cnt(); ++bank) {
    dram.bank = bank;
    physaddr_t phys = dram.phys();
    min_phys = std::min(min_phys, phys);
  }

  return min_phys;
}

/**
 * @brief    Determines the page range for hammering.
 *
 * @param    first_page          First page to use.
 * @param    last_page           Last page to use.
 * @return   true                If the page range could be determined
 *                               successfully.
 * @return   false               If there were errors with determining the page
 *                               range in connection with the configuration.
 */
bool ContiguousFlipFinder::determine_page_range(page_t &first_page,
                                                page_t &last_page) {
  const uint64_t banks_cnt = config.dram_layout.get_banks_cnt();
  uint64_t first_row = config.test_first_row;

  if (first_row == 0) {
    log_info("Determine contiguous pages...");
    if (!find_run(finder, first_page, last_page,
                  banks_cnt * config.test_min_rows * pages_per_row)) {
      log_error("Could not find run of minimum length");
      return false;
    }

    // skip a row so we can be sure we allocated all pages in the first row,
    // this also decreases the chance we flip a bit in memory allocated by
    // another process
    first_row = DRAMAddr(page_2_phys(first_page)).row + 1;
  }

  log_info("Determine contiguous pages starting at row ", first_row, "...");

  page_t first_page_phys = find_first_page_in_row(first_row);
  first_page = phys_2_page(first_page_phys);
  virtaddr_t virt = 0;

  if (!finder.find_page(first_page_phys, virt)) {
    log_error("Could not find first row ", first_row);
    return false;
  }

  if (!find_run_fixed(finder, first_page, last_page,
                      banks_cnt * config.test_min_rows * pages_per_row)) {
    log_error("Could not find ", config.test_min_rows, " rows starting at row ",
              first_row);
    return false;
  }

  if (config.test_last_row > 0) {
    page_t last_page_phys =
        find_first_page_in_row(config.test_last_row + 1) - 1;
    last_page = std::min(last_page, phys_2_page(last_page_phys));
  }

  if (config.test_max_rows > 0) {
    page_t last_page_phys =
        find_first_page_in_row(DRAMAddr(page_2_phys(first_page)).row +
                               config.test_max_rows) -
        1;
    last_page = std::min(last_page, phys_2_page(last_page_phys));
  }

  const std::size_t pages_cnt = last_page - first_page + 1;
  const std::size_t rows_cnt = pages_cnt / (pages_per_row * banks_cnt);
  log_info("Found ", pages_cnt, " contiguous pages");
  log_info("Test ", rows_cnt, " rows/bank...");

  pages_per_bank = pages_cnt / banks_cnt;

  if (pages_per_bank < hammer_pages) {
    log_error("Expected at least ", hammer_pages, " pages per bank, got ",
              pages_per_bank, " pages per bank");
    return false;
  }

  return true;
}

/**
 * @brief    Initializes the hammering process.
 *
 * @param    bank                The banks to hammer.
 * @param    pages_begin         The start page for hammering.
 * @param    pages_end           The last page for hammering.
 * @return   true                If hammer was successful.
 * @return   false               If pages were not found or on do_exit.
 */
bool ContiguousFlipFinder::hammer(
    bank_t bank, const std::vector<page_t>::const_iterator &pages_begin,
    const std::vector<page_t>::const_iterator &pages_end) const {
  if (do_exit) {
    log_trace("do_exit == true");
    return false;
  }

  HammerAddrs addrs;
  addrs.victims.reserve(victim_rows);
  addrs.aggs.reserve(config.aggressor_rows);

  uint32_t offset = 0;

  for (bool is_agg : hammer_pattern) {
    auto it = std::next(pages_begin, offset * pages_per_row);
    assert(std::distance(it, pages_end) >= 0);

    // we must own both physical page in a row
    const physaddr_t &p0 = *it;
    const physaddr_t &p1 = *std::next(pages_begin, offset * pages_per_row + 1);
    assert(p1 - p0 == page_size);

    // the virtual pages must also be in the same row
    virtaddr_t v0, v1;
    assert(finder.find_page(p0, v0) && finder.find_page(p1, v1) &&
           v1 - v0 == page_size);

    if (is_agg) {
      addrs.aggs.push_back(p0);
    } else {
      addrs.victims.push_back(p0);
    }
    ++offset;
  }

  BitFlipper flipper(addrs, temperature_controller);
  if (!flipper.find_pages(finder)) {
    log_error("Could not find physical pages");
    return false;
  }

  log_info("Hammer ", addrs.aggs.size(), " aggressors (bank: ", bank,
           ", rows: [", DRAMAddr(addrs.aggs[0]).row, ", ",
           DRAMAddr(addrs.aggs[addrs.aggs.size() - 1]).row, "])...");

  flipper.hammer();
  return true;
}

/**
 * @brief    Advances one row per iteration and therefore hammers rows multiple
 *           times (like TRRespass).
 *
 */
void ContiguousFlipFinder::default_test() const {
  for (uint64_t offset = 0; offset <= pages_per_bank - hammer_pages;
       offset += pages_per_row) {
    for (const auto &[bank, pages] : banks) {
      auto pages_begin = std::next(pages.begin(), offset);
      auto pages_end = std::next(pages_begin, hammer_pages);
      // assert(pages_end <= pages.end());
      if (pages_end > pages.end()) {
        log_error("pages_end (", std::distance(pages.begin(), pages_end),
                  ") > pages.end() (",
                  std::distance(pages.begin(), pages.end()), ")");
        return;
      }

      if (!hammer(bank, pages_begin, pages_end))
        return;
    }
  }
}

/**
 * @brief    Hammers every row in only one iteration. So that every row only
 *           gets hammered once.
 *
 */
void ContiguousFlipFinder::fast_test() const {
  for (uint64_t offset = 0; offset <= pages_per_bank - hammer_pages;
       offset += hammer_pages - pages_per_row) {
    for (const auto &[bank, pages] : banks) {
      auto pages_begin = std::next(pages.begin(), offset);
      auto pages_end = std::next(pages_begin, hammer_pages);
      assert(pages_end <= pages.end());
      hammer(bank, pages_begin, pages_end);

      std::advance(pages_begin, pages_per_row);
      std::advance(pages_end, pages_per_row);
      if (pages_end <= pages.end() && !hammer(bank, pages_begin, pages_end))
        return;
    }
  }
}

/**
 * @brief    Hammers half the rows in the first bank. Useful for debugging
 *           purposes.
 *
 */
void ContiguousFlipFinder::debug_test() const {
  for (uint64_t offset = 0; offset <= pages_per_bank - hammer_pages;
       offset += hammer_pages - pages_per_row) {
    const uint64_t bank = 0;
    const auto &pages = banks.at(bank);
    auto pages_begin = std::next(pages.begin(), offset);
    auto pages_end = std::next(pages_begin, hammer_pages);
    assert(pages_end <= pages.end());
    if (!hammer(bank, pages_begin, pages_end))
      return;
  }
}

/**
 * @brief    Actuates the hammering process by choosing the configured iteration
 *           algorithm and determining the page range for hammering.
 *
 */
void ContiguousFlipFinder::find_flips() {
  const std::map<std::string, std::function<void()>> iter_algortihms{
    { "default", [this]() { default_test(); } },
    { "fast", [this]() { fast_test(); } },
    { "debug", [this]() { debug_test(); } }
  };

  page_t first_page, last_page;
  if (!determine_page_range(first_page, last_page))
    return;

  banks.clear();
  for (page_t page = first_page; page <= last_page; ++page) {
    physaddr_t phys = page_2_phys(page);
    DRAMAddr dram(phys);
    if (in(dram.bank, config.banks))
      banks[dram.bank].push_back(phys);
  }

  experiment_loop(iter_algortihms.at(config.iter_algorithm));
}
