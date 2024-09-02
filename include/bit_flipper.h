#ifndef BIT_FLIPPER_H
#define BIT_FLIPPER_H

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <set>
#include <vector>
#ifdef USE_ASMJIT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weverything"
#include <asmjit/asmjit.h>
#pragma GCC diagnostic pop
#endif // USE_ASMJIT

#include "dram_address.h"
#include "logging.h"
#include "phys_page_finder.h"
#include "temperature_controller.h"

struct HammerAddrs {
  std::vector<physaddr_t> aggs;
  std::vector<physaddr_t> victims;
};

class BitFlipper {
  const HammerAddrs &phys;
  const TemperatureController &temperature_controller;

  std::vector<virtaddr_t> virt_aggs;
  std::vector<virtaddr_t> virt_victims;

  // Offset, in bytes, of 64-bit victim location from start of page.
  uint64_t flip_offset_bytes;
  // The bit number that changes.
  uint8_t bit_number;
  // 1 if this is a 0 -> 1 bit flip, 0 otherwise.
  uint8_t flips_to = 0;

  std::function<uint64_t()> hammer_aggs = [this]() {
    return hammer_aggs_default();
  };
  uint64_t hammer_aggs_default() const;
  uint64_t hammer_aggs_assembly() const;
  uint64_t hammer_aggs_trrespass() const;
  uint64_t hammer_aggs_machinecode() const;
  bool hammer_and_check(uint64_t victim_init, uint64_t aggressor_init);

#ifdef USE_ASMJIT
  uint64_t hammer_aggs_blacksmith() const;
  void sync_ref(const std::vector<virtaddr_t> &aggressor_pairs,
                asmjit::x86::Assembler &assembler) const;
#endif // USE_ASMJIT

public:
  BitFlipper(const HammerAddrs &phys,
             const TemperatureController &temperature_controller = {})
      : phys(phys), temperature_controller(temperature_controller),
        virt_aggs(phys.aggs.size()), virt_victims(phys.victims.size()) {
    if (config.hammer_algorithm == "default") {
      hammer_aggs = [this]() { return hammer_aggs_default(); };
    } else if (config.hammer_algorithm == "trrespass") {
      hammer_aggs = [this]() { return hammer_aggs_trrespass(); };
    } else if (config.hammer_algorithm == "assembly") {
      hammer_aggs = [this]() { return hammer_aggs_assembly(); };
    } else if (config.hammer_algorithm == "machinecode") {
      hammer_aggs = [this]() { return hammer_aggs_machinecode(); };
#ifdef USE_ASMJIT
    } else if (config.hammer_algorithm == "blacksmith") {
      hammer_aggs = [this]() { return hammer_aggs_blacksmith(); };
#endif // USE_ASMJIT
    } else {
      log_error_and_exit("Invalid hammer algorithm");
    }
  }

  bool find_pages(const PhysPageFinder &finder);
  bool hammer();

  uint64_t get_flip_offset_bytes() const {
    return flip_offset_bytes;
  }

  uint8_t get_bit_number() const {
    return bit_number;
  }

  uint8_t get_flips_to() const {
    return flips_to;
  }
};

#endif // BIT_FLIPPER_H
