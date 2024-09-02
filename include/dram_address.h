#ifndef DRAM_ADDRESS_H
#define DRAM_ADDRESS_H

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

typedef uint64_t physaddr_t;
typedef uintptr_t virtaddr_t;

struct DRAMAddr {
  /* bank is a simplified addressing of <ch,dimm,rk,bg,bk>
     where all this will eventually map to a specific bank */
  uint64_t bank = 0;
  uint64_t row = 0;
  uint64_t col = 0;

  DRAMAddr() = default;
  DRAMAddr(uint64_t bank, uint64_t row, uint64_t col)
      : bank(bank), row(row), col(col) {}
  DRAMAddr(const physaddr_t &p_addr);

  std::string str() const;
  physaddr_t phys() const;

  operator std::string() const {
    return str();
  }

  bool operator==(const DRAMAddr &other) {
    return bank == other.bank && row == other.row && col == other.col;
  }

  bool equal_row(const DRAMAddr &other) {
    return bank == other.bank && row == other.row;
  }

private:
  static uint64_t get_dram_row(const physaddr_t &p_addr);
  static uint64_t get_dram_col(const physaddr_t &p_addr);
};

std::ostream &operator<<(std::ostream &out, const DRAMAddr &d_addr);

#endif // DRAM_ADDRESS_H
