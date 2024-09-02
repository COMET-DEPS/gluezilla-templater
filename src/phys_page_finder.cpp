/**
 * @file     phys_page_finder.cpp
 *
 * @brief    Handles the memory allocation and search for physical pages.
 *
 */

#include <cassert>
#include <fstream>
#include <limits>
#include <regex>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

// should be defined in mman.h
#ifndef MAP_HUGE_2MB
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)
#endif
#ifndef MAP_HUGE_1GB
#define MAP_HUGE_1GB (30 << MAP_HUGE_SHIFT)
#endif

#include "config.h"
#include "logging.h"

#include "phys_page_finder.h"

/**
 * @brief    Extracts the physical page number from a Linux /proc/PID/pagemap
 *           entry.
 *
 * @param    pagemap_entry       The pagemap entry.
 * @return   uint64_t            The frame number.
 */
uint64_t frame_number_from_pagemap(uint64_t pagemap_entry) {
  // Bits 0-54: page frame number (PFN) if present
  return pagemap_entry & ((1ULL << 55) - 1);
}

/**
 * @brief    Extracts the page present bit from a Linux /proc/PID/pagemap entry.
 *
 * @param    pagemap_entry       The pagemap entry.
 * @return   uint64_t            The page present bit.
 */
uint64_t is_page_present(uint64_t pagemap_entry) {
  // Bit 63: page present
  return pagemap_entry & (1ULL << 63);
}

/**
 * @brief    Translates a virtual address to a physical address.
 *
 * @param    virtual_addr        The virtual address.
 * @return   uint64_t            The physical address.
 */
uint64_t get_physical_addr(virtaddr_t virtual_addr) {
  int fd = open("/proc/self/pagemap", O_RDONLY);
  assert(fd >= 0);

  off_t pos = lseek(fd, (virtual_addr / page_size) * 8, SEEK_SET);
  assert(pos >= 0);
  uint64_t value;
  int got = read(fd, &value, 8);
  assert(got == 8);
  int rc = close(fd);
  assert(rc == 0);

  // Check the "page present" flag.
  assert(value & (1ULL << 63) && "page not in memory (swapped)");

  uint64_t frame_num = frame_number_from_pagemap(value);
  assert(frame_num > 0 && "executed as root?");
  return (frame_num * page_size) | (virtual_addr & (page_size - 1));
}

/**
 * @brief    Translates a virtual address to a physical address.
 *
 * @param    virtual_addr        The virtual address.
 * @param    fd                  File descriptor of the pagemap to use.
 * @return   uint64_t            The physical address.
 */
uint64_t get_physical_addr(virtaddr_t virtual_addr, int fd) {
  off_t pos = lseek(fd, (virtual_addr / page_size) * 8, SEEK_SET);
  assert(pos >= 0);
  uint64_t value;
  int got = read(fd, &value, 8);
  assert(got == 8);

  // Check the "page present" flag.
  assert(value & (1ULL << 63) && "page not in memory (swapped)");

  uint64_t frame_num = frame_number_from_pagemap(value);
  assert(frame_num > 0 && "executed as root?");
  return (frame_num * page_size) | (virtual_addr & (page_size - 1));
}

/**
 * @brief    Construct a new PhysPageFinder object used for finding physical
 *           pages.
 *
 */
PhysPageFinder::PhysPageFinder() {

  // Check free hugepages
  if (config.alloc_page_size != "4kb") {

    uint32_t free_hugepages = get_hugepage_count();

    if (free_hugepages != 0) {
      if (config.use_free_memory) {
        config.hugepage_count = free_hugepages;
        log_info("Found ", free_hugepages, " free hugepages");
      } else if (config.hugepage_count > free_hugepages) {
        log_error_and_exit("Found ", free_hugepages,
                           " free hugepages, configuration requested ",
                           config.hugepage_count);
      }
    }
  }

  // choose allocation type
  if (config.alloc_page_size == "1gb") {
    check_hugepagesize();
    config.memory_size = 1_GiB * config.hugepage_count;
    alloc_hugepages(true);
  } else if (config.alloc_page_size == "2mb") {
    check_hugepagesize();
    config.memory_size = 2_MiB * config.hugepage_count;
    alloc_hugepages(false);
  } else {
    alloc_default();
  }

  uint64_t &memory_size = config.memory_size;

  log_info("Building page map...");
  int fd = open("/proc/self/pagemap", O_RDONLY);
  assert(fd >= 0);

  const uint64_t buffer_size = 1_KiB;
  const uint64_t read_pages = buffer_size / sizeof(uint64_t);
  uint64_t buffer[read_pages];

  for (uint64_t i = 0; i < memory_size / page_size; i += read_pages) {
    ssize_t got = pread64(fd, buffer, read_pages * sizeof(uint64_t),
                          (mem / page_size + i) * sizeof(uint64_t));
    for (uint64_t j = 0; j < got / sizeof(uint64_t); ++j) {
      if (!is_page_present(buffer[j])) // no valid frame number in this case
        continue;

      uint64_t frame = frame_number_from_pagemap(buffer[j]);
      uint64_t page_offset = i + j;
      assert(frame <= std::numeric_limits<uint32_t>::max());
      assert(page_offset <= std::numeric_limits<uint32_t>::max());
      pagemap[static_cast<uint32_t>(frame)] =
          static_cast<uint32_t>(page_offset);
    }
  }

  int rc = close(fd);
  assert(rc == 0);
}

/**
 * @brief    Default memory allocation. Allocates 4 KiB pages.
 *
 */
void PhysPageFinder::alloc_default() {
  log_info("Using default allocation...");
  uint64_t &memory_size = config.memory_size;
  log_info("Allocate ", memory_size, " bytes (", memory_size >> 30, " GiB)...");
  mem = reinterpret_cast<virtaddr_t>(
      mmap(NULL, memory_size, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANON | MAP_POPULATE | MAP_NORESERVE, -1, 0));
  assert(mem != reinterpret_cast<virtaddr_t>(MAP_FAILED));
}

/**
 * @brief    Allocation of hugepages. Allocates 2 MiB or 1 GiB hugepages.
 *
 * @param    use_1gb_hugepages   Use 1GiB hugepages?
 */
void PhysPageFinder::alloc_hugepages(bool use_1gb_hugepages) {
  uint32_t hugepage_count = config.hugepage_count;
  uint64_t length = (use_1gb_hugepages ? 1_GiB : 2_MiB) * hugepage_count;
  log_info("Using ", hugepage_count, " ", use_1gb_hugepages ? "1GB" : "2MB",
           " hugepages for allocation...");

  mem = reinterpret_cast<virtaddr_t>(
      mmap(NULL, length, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_POPULATE | MAP_ANONYMOUS | MAP_HUGETLB |
               (use_1gb_hugepages ? MAP_HUGE_1GB : MAP_HUGE_2MB),
           -1, 0));
  assert(mem != reinterpret_cast<virtaddr_t>(MAP_FAILED));
}

/**
 * @brief    Gets the number of free hugepages.
 *
 * @return   uint64_t            The number of free hugepages.
 */
uint64_t PhysPageFinder::get_hugepage_count() {
  std::ifstream infile("/proc/meminfo");
  std::string line;
  std::smatch match;
  std::regex regexp("HugePages_Free: *([0-9]*)");

  while (std::getline(infile, line)) {
    if (std::regex_search(line, match, regexp)) {
      return std::stoull(match[1]);
    }
  }

  log_error("Could not retrieve number of free hugepages");
  return 0;
}

/**
 * @brief    Checks if the hugepage size of the system matches the one
 *           configured.
 *
 * @return   true                If hugepage size matches.
 * @return   false               If hugepage size differs.
 */
bool PhysPageFinder::check_hugepagesize() {
  std::ifstream infile("/proc/meminfo");
  std::string line;
  std::smatch match;
  std::regex regexp("Hugepagesize: *([0-9]*) kB");

  while (std::getline(infile, line)) {
    if (std::regex_search(line, match, regexp)) {
      uint64_t hugepagesize = std::stoull(match[1]);

      if ((config.alloc_page_size == "1gb" && hugepagesize * 1_KiB == 1_GiB) ||
          (config.alloc_page_size == "2mb" && hugepagesize * 1_KiB == 2_MiB))
        return true;

      log_error_and_exit("Hugepagesize of system (", hugepagesize,
                         " KiB) and configuration (", config.alloc_page_size,
                         ") do not match");
    }
  }

  log_error("Could not retrieve hugepage size from /proc/meminfo");
  return false;
}

/**
 * @brief    Checks if a page is available in the pagemap.
 *
 * @param    phys_addr           The physical address of the page.
 * @param    virt_addr           The virtual address of the page.
 * @return   true                If the page is available.
 * @return   false               If the page is not available.
 */
bool PhysPageFinder::find_page(uint64_t phys_addr,
                               virtaddr_t &virt_addr) const {
  auto it = pagemap.find(phys_addr / page_size);
  if (it == pagemap.end())
    return false;
  virt_addr = mem + static_cast<virtaddr_t>(it->second) * page_size;
  return true;
}
