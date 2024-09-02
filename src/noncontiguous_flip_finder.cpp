/**
 * @file     noncontiguous_flip_finder.cpp
 *
 * @brief    Contains the noncontiguoues flip finder used for hammering over a
 *           noncontigous memory space.
 *
 */

#include <algorithm>
#include <cassert>
#include <csignal>
#include <cstddef>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "db.h"
#include "logging.h"
#include "operators.h"

#include "noncontiguous_flip_finder.h"

/**
 * @brief    Gets the page bounds.
 *
 * @return   std::pair<page_t, page_t>
 *                               The lower and upper page bound.
 */
std::pair<page_t, page_t> NoncontiguousFlipFinder::get_page_bounds() const {
  // PhysPageFinder manages an ordered map
  page_t first_page = finder.front().first;
  page_t last_page = finder.back().first;
  assert(last_page >= first_page);
  return { first_page, last_page };
}

/**
 * @brief    Gets the row bounds.
 *
 * @param    bank                The banks to use.
 * @param    first_page          The first page.
 * @param    last_page           The last page.
 * @return   std::pair<row_t, row_t>
 *                               The lower and upper row bound.
 */
std::pair<row_t, row_t>
NoncontiguousFlipFinder::get_row_bounds(bank_t bank, page_t first_page,
                                        page_t last_page) const {
  row_t first_row = UINT64_MAX;
  row_t last_row = 0;

  // assuming that a higher row can't have a lower physical address
  for (page_t p = first_page; p <= last_page; p++) {
    if (finder.contains(p)) {
      const DRAMAddr dram(page_2_phys(p));
      if (dram.bank == bank) {
        first_row = dram.row;
        break;
      }
    }
  }

  // assuming that a lower row can't have a higher physical address
  for (page_t p = last_page; p-- > 0;) {
    if (finder.contains(p)) {
      const DRAMAddr dram(page_2_phys(p));
      if (dram.bank == bank) {
        last_row = dram.row;
        break;
      }
    }
  }

  if (config.test_first_row != 0) {
    first_row = std::max(first_row, config.test_first_row);
  }

  if (config.test_last_row != 0) {
    last_row = std::min(last_row, config.test_last_row);
  }

  assert(last_row >= first_row);
  return { first_row, last_row };
}

/**
 * @brief    Finds missing rows between first and last page.
 *
 * @param    first_page          The first page.
 * @param    last_page           The last page.
 */
void NoncontiguousFlipFinder::find_missing_rows(page_t first_page,
                                                page_t last_page) {
  for (page_t page = first_page; page <= last_page; ++page) {
    if (!finder.contains(page)) {
      const DRAMAddr dram(page_2_phys(page));
      // row is missing if any of its two pages is missing
      missing_rows[dram.bank].insert(missing_rows[dram.bank].end(), dram.row);
    }
  }
}

/**
 * @brief    Makes sure if we own all pages in [first_row - row_padding,
 *           last_row + row_padding] to prevent bit flips in pages we don't own.
 *
 * @param    bank                The banks to check.
 * @param    first_row           The first row to check.
 * @param    last_row            The last row to check.
 * @return   true                If any row is missing.
 * @return   false               If no row is missing.
 */
bool NoncontiguousFlipFinder::is_any_row_missing(bank_t bank, row_t first_row,
                                                 row_t last_row) const {
  // make sure we own all pages in
  // [first_row - row_padding, last_row + row_padding]
  // to prevent bit flips in pages we don't own
  const auto &padding = config.row_padding;
  const auto lb = missing_rows.at(bank).lower_bound(first_row - padding);
  return lb != missing_rows.at(bank).end() && *lb <= last_row + padding;
}

/**
 * @brief    Initializes the hammering process.
 *
 * @param    bank                The banks to hammer.
 * @param    first_victim        The first victim row.
 * @param    last_victim         The last victim row.
 * @return   true                If the experiment should be continued.
 * @return   false               If the experiment loop should be stopped.
 */
bool NoncontiguousFlipFinder::hammer(bank_t bank, row_t first_victim,
                                     row_t last_victim) const {
  if (is_any_row_missing(bank, first_victim, last_victim))
    return true;

  HammerAddrs addrs;
  addrs.victims.reserve(victim_rows);
  addrs.aggs.reserve(config.aggressor_rows);

  uint32_t offset = 0;
  DRAMAddr dram(bank, 0, 0);

  for (bool is_agg : hammer_pattern) {
    dram.row = first_victim + offset;
    physaddr_t phys = dram.phys();

    if (is_agg) {
      addrs.aggs.push_back(phys);
    } else {
      addrs.victims.push_back(phys);
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

  if (do_exit) {
    log_trace("do_exit == true");
  }

  return !do_exit;
}

/**
 * @brief    Advances one row per iteration and therefore hammers rows multiple
 *           times (like TRRespass).
 *
 */
bool NoncontiguousFlipFinder::default_test(bank_t bank, row_t first_row,
                                           row_t last_row) const {
  for (row_t row = first_row; row <= last_row - hammer_rows + 1; ++row) {
    const row_t first_victim = row;
    const row_t last_victim = row + hammer_rows - 1;

    if (!hammer(bank, first_victim, last_victim))
      return false;
  }

  return true;
}

/**
 * @brief    Hammers every row in only one iteration. So that every row only
 *           gets hammered once.
 *
 */
bool NoncontiguousFlipFinder::fast_test(bank_t bank, row_t first_row,
                                        row_t last_row) const {
  // this won't test the last n rows in a block if n < hammer_rows
  for (row_t row = first_row;
       row <=
       last_row - hammer_rows + 1;
       row += hammer_rows - 1) {
    const row_t first_victim = row;
    const row_t last_victim = row + hammer_rows - 1;

    if (!hammer(bank, first_victim, last_victim) ||
        !hammer(bank, first_victim + 1, last_victim + 1))
      return false;
  }

  return true;
}

/**
 * @brief    Hammers half the rows in the first bank. Useful for debugging
 *           purposes.
 *
 */
bool NoncontiguousFlipFinder::debug_test(bank_t bank, row_t first_row,
                                         row_t last_row) const {
  for (row_t row = first_row; row <= last_row - hammer_rows + 1;
       row += hammer_rows - 1) {
    const row_t first_victim = row;
    const row_t last_victim = row + hammer_rows - 1;

    if (!hammer(bank, first_victim, last_victim))
      return false;
  }

  return false; // test only first bank
}

/**
 * @brief    Actuates the hammering process by choosing the configured iteration
 *           algorithm and determining the page range for hammering.
 *
 */
void NoncontiguousFlipFinder::find_flips() {
  const std::map<std::string, std::function<bool(bank_t, row_t, row_t)>>
      iter_algorithms{
        { "default", [this](auto... p) { return default_test(p...); } },
        { "fast", [this](auto... p) { return fast_test(p...); } },
        { "debug", [this](auto... p) { return debug_test(p...); } }
      };

  const auto [first_page, last_page] = get_page_bounds();
  find_missing_rows(first_page, last_page);

  // init-captures required to capture a structured binding in Clang
  // (restriction was removed in C++20 but this is not yet impl. in Clang 12)
  // https://burnicki.pl/en/2021/04/19/capture-structured-bindings.html
  experiment_loop([this, &first_page = first_page, &last_page = last_page,
                   &iter_algorithms]() {
    for (const bank_t &bank : config.banks) {
      auto [first_row, last_row] = get_row_bounds(bank, first_page, last_page);

      log_info("Testing bank ", bank, ": rows [", first_row, ", ", last_row,
               "], missing rows: ", missing_rows[bank].size());

      if (!iter_algorithms.at(config.iter_algorithm)(bank, first_row, last_row))
        break;
    }
  });
}
