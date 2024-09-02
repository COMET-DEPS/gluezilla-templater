#ifndef FLIP_FINDER_H
#define FLIP_FINDER_H

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <functional>
#include <vector>

#include "config.h"
#include "dram_address.h"
#include "phys_page_finder.h"
#include "temperature_controller.h"

// is true when process was interrupted (Ctrl+C)
inline volatile std::sig_atomic_t do_exit = false;

class FlipFinder {
protected:
  const PhysPageFinder &finder;
  TemperatureController temperature_controller;
  HammerPattern hammer_pattern;
  uint32_t victim_rows;
  uint32_t hammer_rows;

  // returns the number of the page that contains the specified physical address
  static inline page_t phys_2_page(physaddr_t phys) {
    static_assert(page_size == 4_KiB);
    return static_cast<page_t>(phys) >> 12;
  }

  // returns the physical starting address of the page with the specified number
  static inline physaddr_t page_2_phys(page_t page) {
    static_assert(page_size == 4_KiB);
    return static_cast<physaddr_t>(page) << 12;
  }

  FlipFinder(const PhysPageFinder &finder)
      : finder(finder), temperature_controller(),
        hammer_pattern(config.hammer_pattern),
        victim_rows(
            std::count(hammer_pattern.begin(), hammer_pattern.end(), 0)),
        hammer_rows(hammer_pattern.size()) {}

  void repetition_loop(std::function<void()> iter_algorithm,
                       int64_t target_temperature = 0);
  void experiment_loop(std::function<void()> iter_algorithm);

public:
  virtual void find_flips() = 0;
  virtual ~FlipFinder() {}
};

#endif // FLIP_FINDER_H
