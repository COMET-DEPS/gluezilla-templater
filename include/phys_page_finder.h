#ifndef PHYS_PAGE_FINDER_H
#define PHYS_PAGE_FINDER_H

#include <cstdint>
#include <iterator>
#include <map>

#include "config.h"

uint64_t get_physical_addr(virtaddr_t virtual_addr);
uint64_t get_physical_addr(virtaddr_t virtual_addr, int fd);

class PhysPageFinder {
  virtaddr_t mem;
  // maps frame numbers to page offset (relative to mem)
  // (uses uint32_t instead of uint64_t to save space,
  // should work for up to 16 TiB of main memory (checked with assertion))
  typedef std::map<uint32_t, uint32_t> pagemap_t;
  pagemap_t pagemap;

  void alloc_default();
  void alloc_hugepages(bool use_1gb_hugepages = false);
  static uint64_t get_hugepage_count();
  static bool check_hugepagesize();

public:
  PhysPageFinder();
  bool find_page(uint64_t phys_addr, virtaddr_t &virt_addr) const;

  bool contains(page_t phys_page) const {
    return pagemap.contains(phys_page);
  }

  std::size_t size() const {
    return pagemap.size();
  }

  pagemap_t::const_iterator begin() const {
    return pagemap.begin();
  }

  pagemap_t::const_iterator end() const {
    return pagemap.end();
  }

  const pagemap_t::value_type &front() const {
    return *pagemap.begin();
  }

  const pagemap_t::value_type &back() const {
    return *std::prev(pagemap.end());
  }
};

#endif // PHYS_PAGE_FINDER_H
