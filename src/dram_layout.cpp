/**
 * @file     dram_layout.cpp
 *
 * @brief    Used for saving and outputting the DRAM Layout containing mapping
 *           functions and row/col masks.
 *
 */

#include <cstddef>
#include <iomanip>
#include <sstream>

#include "operators.h"

#include "dram_layout.h"

/**
 * @brief    Returns the DRAM Layout as a string.
 *
 * @return   std::string         The DRAM Layout.
 */
std::string DRAMLayout::str() const {
  std::ostringstream ss;
  ss << *this;
  return ss.str();
}

/**
 * @brief    Overloads the << operator for outputting the DRAM Layout as a
 *           string.
 *
 * @param    out                 The output stream to use.
 * @param    d_layout            The dram layout.
 * @return   std::ostream&       The output stream.
 */
std::ostream &operator<<(std::ostream &out, const DRAMLayout &d_layout) {
  std::string sep;
  const auto flags = out.flags();
  out << std::showbase << std::hex << "fns: " << d_layout.h_fns
      << ", row: " << d_layout.row_masks << ", col: " << d_layout.col_masks;
  out.flags(flags);
  return out;
}
