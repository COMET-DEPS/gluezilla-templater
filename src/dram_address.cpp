/**
 * @file     dram_address.cpp
 *
 * @brief    Contains everything regarding transforming physical DRAM addresses
 *           into a bank, row, col representation and back.
 *
 */

#include <cassert>
#include <cstddef>
#include <iomanip>
#include <numeric>
#include <sstream>

#include "config.h"
#include "logging.h"
#include "operators.h"

#include "dram_address.h"

#define DEBUG_REVERSE_FN 1

/**
 * @brief    Construct a new DRAMAddr::DRAMAddr object
 *
 * @param    p_addr              The physical dram address.
 */
DRAMAddr::DRAMAddr(const physaddr_t &p_addr) {
  for (uint64_t i = 0; i < config.dram_layout.h_fns.size(); i++) {
    bank |= xor_bits(p_addr, config.dram_layout.h_fns[i]) << i;
  }

  row = get_dram_row(p_addr);
  col = get_dram_col(p_addr);
}

/**
 * @brief    Returns the DRAM address as a string.
 *
 * @return   std::string         The DRAM address.
 */
std::string DRAMAddr::str() const {
  std::ostringstream ss;
  ss << *this;
  return ss.str();
}

uint64_t pop_least_significant_bits(uint64_t &val, int n) {
  uint64_t res = val & ((1 << n) - 1);
  val = val >> n;
  return res;
}

/**
 * @brief    Translates the DRAM address in form of bank, row, col to the
 *           physical address.
 *
 * @return   physaddr_t          The physical DRAM address.
 */
physaddr_t DRAMAddr::phys() const {
  physaddr_t p_addr = 0;
  // uint64_t col_val = 0;
  const auto &h_fns = config.dram_layout.h_fns;
  const auto &row_masks = config.dram_layout.row_masks;
  const auto &col_masks = config.dram_layout.col_masks;

  if (row_masks.size() == 1) {
    p_addr = (row << count_trailing_zero_bits(row_masks[0])); // set row bits
  } else {
    uint64_t r = row;
    for (const auto &row_mask : row_masks) {
      const int n = count_one_bits(row_mask);
      const int offset = count_trailing_zero_bits(row_mask);
      const uint64_t bits = pop_least_significant_bits(r, n);
      p_addr |= bits << offset;
    }
    assert(r == 0);
  }

  if (col_masks.size() == 1) {
    p_addr |= col << count_trailing_zero_bits(col_masks[0]); // set col bits
  } else {
    uint64_t c = col;
    for (const auto &col_mask : col_masks) {
      const int n = count_one_bits(col_mask);
      const int offset = count_trailing_zero_bits(col_mask);
      const uint64_t bits = pop_least_significant_bits(c, n);
      p_addr |= bits << offset;
    }
    assert(c == 0);
  }

  const auto not_row_mask =
      ~std::accumulate(row_masks.begin(), row_masks.end(), 0ull);
  const auto not_col_mask =
      ~std::accumulate(col_masks.begin(), col_masks.end(), 0ull);

  for (uint64_t i = 0; i < h_fns.size(); i++) {
    // if the address already respects the h_fn then just move to the next
    // func
    if (xor_bits(p_addr, h_fns[i]) == ((bank >> i) & 1l)) {
      continue;
    }
    // else flip a bit of the address so that the address respects the dram
    // h_fn that is get only bits not affecting the row.
    uint64_t h_lsb =
        count_trailing_zero_bits(h_fns[i] & not_col_mask & not_row_mask);
    p_addr ^= 1 << h_lsb;
  }

#if DEBUG_REVERSE_FN
  bool correct = true;
  for (uint64_t i = 0; i < h_fns.size(); i++) {
    if (xor_bits(p_addr, h_fns[i]) != ((bank >> i) & 1l)) {
      correct = false;
      break;
    }
  }
  if (row != get_dram_row(p_addr))
    correct = false;
  if (!correct)
    log_error("Mapping function for ", std::showbase, std::hex, p_addr,
              " not respected");
#endif

  return p_addr;
}

/**
 * @brief    Gets the row of a physical memory address.
 *
 * @param    p_addr              The physical memory address.
 * @return   uint64_t            The row.
 */
uint64_t DRAMAddr::get_dram_row(const physaddr_t &p_addr) {
  const auto &row_masks = config.dram_layout.row_masks;

  if (row_masks.size() == 1)
    return (p_addr & row_masks[0]) >> count_trailing_zero_bits(row_masks[0]);

  uint64_t row = 0;
  uint64_t offset = 0;
  for (const auto &row_mask : row_masks) {
    row += ((p_addr & row_mask) >> count_trailing_zero_bits(row_mask))
           << offset;
    offset += count_one_bits(row_mask);
  }

  return row;
}

/**
 * @brief    Gets the column of a physical memory address.
 *
 * @param    p_addr              The physical memory address.
 * @return   uint64_t            The column.
 */
uint64_t DRAMAddr::get_dram_col(const physaddr_t &p_addr) {
  const auto &col_masks = config.dram_layout.col_masks;

  if (col_masks.size() == 1)
    return (p_addr & col_masks[0]) >> count_trailing_zero_bits(col_masks[0]);

  uint64_t col = 0;
  uint64_t offset = 0;
  for (const auto &col_mask : col_masks) {
    col += ((p_addr & col_mask) >> count_trailing_zero_bits(col_mask))
           << offset;
    offset += count_one_bits(col_mask);
  }

  return col;
}

/**
 * @brief    Overloads the << operator for outputting a DRAM Address in the form
 *           of of bank, row, col.
 *
 * @param    out                 The output stream to use.
 * @param    d_addr              The dram address.
 * @return   std::ostream&       The output stream.
 */
std::ostream &operator<<(std::ostream &out, const DRAMAddr &d_addr) {
  const char prev_fill = out.fill('0');
  ios_fmt_saver ifs(out);
  out << std::right << "(bank: " << std::setw(2) << d_addr.bank
      << ", row: " << std::setw(8) << d_addr.row << ", col: " << std::setw(4)
      << d_addr.col << ")";
  out.fill(prev_fill);
  return out;
}
