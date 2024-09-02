#ifndef CONFIG_H
#define CONFIG_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "db.h"
#include "dram_address.h"
#include "dram_layout.h"
#include "hammer_pattern.h"
#include "operators.h"

// should not be changed
const uint16_t page_size = 4_KiB;
const uint16_t row_size = 8_KiB;
const uint16_t pages_per_row = row_size / page_size;

typedef uint64_t page_t;
typedef uint64_t row_t;
typedef uint64_t bank_t;

/**
 * @brief    Covers everything regarding the configuration of the tool and sets
 *           default values.
 */
class Config {
public:
  // [dram_layout]
  /**
   * The Mapping Functions, which are used to translate between virtual and
   * physical addresses. Mapping functions consist of the functions which are
   * used to determine the rank and the bank, the row_mask which determines
   * which bits are used for the row and the col_mask which is also used to
   * determine the column.
   */
  DRAMLayout dram_layout = {
    // functions
    { 0x2040, 0x44000, 0x88000, 0x110000, 0x220000 },
    // row_masks
    { 0xffffc0000 },
    // col_masks
    { ((1 << 13) - 1) }
  };

  // [memory]
  /**
   * The size of the pages to allocate ("4kb", "2mb", "1gb").
   * Hugepages must be enabled in the bootloader configuration (2mb, 1gb), e.g.,
   * when using GRUB on Ubuntu, edit `/etc/default/grub` and run `update-grub`:
   * ```
   * GRUB_CMDLINE_LINUX="hugepagesz=1GB default_hugepagesz=1GB hugepages=6"
   * GRUB_CMDLINE_LINUX="hugepagesz=2MB default_hugepagesz=2MB hugepages=14000"
   * ```
   */
  std::string alloc_page_size = "4kb";
  /**
   * The file the page allocation data should be exported to (might be useful
   * for debugging; leave empty to disable file export)
   */
  std::string page_allocation_file = "";
  /**
   * If `alloc_page_size == "4kb"`, automatically determines #memory_size;
   * otherwise automatically determines #hugepage_count.
   */
  bool use_free_memory = true;
  /**
   * Allocate certain percentage of free memory (if `alloc_page_size == "4kb"`).
   */
  float allocate_percentage = 0.99;
  /**
   * Size of the memory that should be allocated (if `alloc_page_size == "4kb"`
   * and `use_free_memory == false`).
   */
  uint64_t memory_size = 16_GiB;
  /**
   * Number of hugepages to use (if `alloc_page_size != "4kb"` and
   * `use_free_memory == false`; bootloader must be configured accordingly).
   */
  uint32_t hugepage_count = 1;

  // [hammer]
  /**
   * Times the experiment should be executed.
   */
  uint32_t experiment_repetitions = 1;
  /**
   * Align the hammering to refresh ops, looking at memory latency in CPU
   * cycles (only if `hammer_algorithm == "trrespass"`).
   */
  uint32_t threshold = 0;
  /**
   * Times the Rowhammer loop should be executed per test
   * (minimum value that caused bit flips: 10000).
   */
  uint64_t hammer_count = 1000000;
  /**
   * Number of rows that should be hammered.
   */
  uint32_t aggressor_rows = 24;
  /**
   * Which memory allocation method should be used:
   * - "contiguous":    use #ContiguousFlipFinder
   * - "noncontiguous": use #NoncontiguousFlipFinder
   */
  std::string memory_allocator = "noncontiguous";
  /**
   * Which algorithm should be used to iterate the allocated memory:
   * - "default"  iterate + 1 row every time
   * - "fast"     hammer each area only once (iterate + [pattern size])
   * - "debug"
   */
  std::string iter_algorithm = "default";
  /**
   * A comma-separated list of banks that should be hammered (if left empty,
   * all banks are hammered).
   */
  std::vector<bank_t> banks;
  /**
   * Rows at the beginning and end of a contiguous range of allocated rows that
   * should be used as additional victim rows.
   */
  uint32_t row_padding = 10;
  /**
   * Any pattern of v's (victims), a's (aggressors), x's (random row distances),
   * e.g.:
   * - "va"         (= n-sided)
   * - "vavavvvvav" (= assisted double-sided)
   * - "avax"       (= randomly in random_pattern_area distributed agg pairs)
   *
   * The pattern is repeated until number of a's >= aggressor_rows and a victim
   * row is added at the end if the pattern does not end in v.
   */
  HammerPattern hammer_pattern = { "va" };
  /**
   * Row area on which the hammer pattern with x's (random row distances)
   * should be spread.
   */
  uint64_t random_pattern_area;
  /**
   * Supported hammer algorithm implementations:
   * - "default":     Google's rowhammer-test's algorithm
   * - "trrespass":   TRRespass's hammer algorithm
   * - "blacksmith":  Blacksmith's hammer algorithm (requires AsmJit)
   * - "assembly":    partially implemented in assembly code
   * - "machinecode": use dynamically generated machine code
   */
  std::string hammer_algorithm = "default";
  /**
   * Define the nop count executed between accessing an aggressor row and
   * flushing it (only if `hammer_algorithm == "machinecode"`).
   */
  uint64_t nop_count = 80;
  /**
   * Initial value(s) for the victim rows (comma-separated).
   */
  std::vector<uint64_t> victim_init = { 0, ~0ull }; // "0x00,0xff"
  /**
   * Initial value(s) for the aggressor rows (comma-separated, leave empty to
   * use inverted victim_init).
   */
  std::vector<uint64_t> aggressor_init = { ~0ull, 0 }; // "0xff,0x00"
  /**
   * The minimum number of rows to test per bank (aggressors rows + victim rows;
   * only if `memory_allocator == "contiguous"`).
   */
  uint64_t test_min_rows = (aggressor_rows * 2) + 1;
  /**
   * The maximum number of rows to test per bank (aggressors rows + victim
   * rows; only if `memory_allocator == "contiguous"`; use 0 for no
   * restriction).
   */
  uint64_t test_max_rows = 0;
  /**
   * The first row that should be tested (use 0 to determine automatically).
   */
  row_t test_first_row = 0;
  /**
   * The last row that should be tested (use 0 to determine automatically).
   */
  row_t test_last_row = 0;
  /**
   * Timeout for experiment (use 0 to disable timeout).
   */
  std::chrono::seconds test_max_time;

#ifdef USE_ASMJIT
  // [blacksmith]
  /**
   * The access order of the aggressors specified as a comma-separated list of
   * aggressor IDs.
   */
  std::vector<uint64_t> hammer_order;
  /**
   * The count of aggressors used for syncing to the refresh.
   */
  uint64_t num_aggs_for_sync = 2;
  /**
   * The total number of memory row activations.
   */
  uint64_t total_num_activations = 5000000;
  /**
   * flushing strategy:
   * Determines if an aggressor should be flushed directly after access or
   * before accessing it again.
   *
   * - "earliest_possible": directly after access
   * - "latest_possible": before accessing
   */
  std::string flushing = "earliest_possible";
  /**
   * fencing strategy:
   * Ensures that aggressors are accessed in the correct order.
   *
   * - "earliest_possible": not implemented though still present in Blacksmith
   * - "latest_possible"
   */
  std::string fencing = "latest_possible";
#endif // USE_ASMJIT

  // [temperature]
  /**
   * The device (usb) on which the temperature controller is connected (file)
   * (leave empty to disable this feature and ignore the following settings)
   */
  std::string device = "";
  /**
   * A comma-separated list of temperatures that should be tested (in °C).
   */
  std::vector<int64_t> target_temps = {};
  /**
   * The interval in which the temperature may vary (target +- interval; in °C).
   */
  uint64_t interval = 3;
  /**
   * How long the temperature change may last before it timeouts (in minutes).
   */
  std::chrono::seconds timeout;

#ifdef USE_DB
  // [db]
  /**
   * The filename (and path) of the database where the data should be saved to.
   */
  std::string db_filepath;
  // [db.config]
  /**
   * The memory modules used. Automatically detected, by serial number, if
   * available in dimm_ids.
   */
  std::vector<std::string> dimms;
  // [db.dimm_ids]
  /**
   * The known memory modules (serial_number=module_number), e.g.:
   *
   * 0x395C99B0=4S9
   * ...
   */
  std::map<std::string, std::string> dimm_ids;
  // [db.bios_settings]
  /**
   * The BIOS settings (key=value), e.g.:
   *
   * tREFI=65535
   * ...
   */
  std::map<std::string, std::string> bios_settings;
  // [db.experiments]
  /**
   * The comment, which should be added to an executed experiment.
   */
  std::string experiment_comment;
#endif

  bool read(const std::string &filename = "config.ini");

private:
  std::map<std::string, std::map<std::string, std::string>> ini;

  void read_values();
  void verify_values();

  void parse(std::istream &in);
  static bool verify_masks(const std::vector<uint64_t> masks);
  static uint64_t parse_init_pattern(std::string s);

  void get_value(const std::string &section, const std::string &key,
                 std::string &value) const;
  void get_value(const std::string &section, const std::string &key,
                 uint64_t &value) const;
  void get_values(
      const std::string &section, const std::string &key,
      std::vector<uint64_t> &values,
      const std::function<uint64_t(std::string)> &parse = [](const auto &s) {
        return std::stoull(s, nullptr, 0);
      }) const;
  void get_values(
      const std::string &section, const std::string &key,
      std::vector<int64_t> &values,
      const std::function<int64_t(std::string)> &parse = [](const auto &s) {
        return std::stoll(s, nullptr, 0);
      }) const;

  template <template <typename> class Container>
  void get_values(const std::string &section, const std::string &key,
                  Container<std::string> &values) const {
    if (ini.contains(section) && ini.at(section).contains(key)) {
      std::string str = ini.at(section).at(key);
      values.clear();
      split(str, values, ',');
    }
  }

  template <typename T>
  void get_value(const std::string &section, const std::string &key,
                 T &value) const {
    if (ini.contains(section) && ini.at(section).contains(key)) {
      std::istringstream ss(ini.at(section).at(key));
      ss >> value;
    }
  }

  template <typename T>
  void get_value(const std::string &section, const std::string &key, T &value,
                 const std::function<T(std::string)> &parse) const {
    if (ini.contains(section) && ini.at(section).contains(key))
      value = parse(ini.at(section).at(key));
  }

  template <typename R, typename P>
  void get_value(const std::string &section, const std::string &key,
                 std::chrono::duration<R, P> &value) const {
    // should be reimplemented using std::chrono::parse once available
    // std::get_time should not be used at it does not support durations

    if (ini.contains(section) && ini.at(section).contains(key)) {
      std::vector<uint64_t> items;
      split(ini.at(section).at(key), items, ':');

      if (items.size() < 1 || items.size() > 3) {
        value = {};
        return;
      }

      value = std::chrono::seconds(items.back());
      items.pop_back();
      if (!items.empty()) {
        value += std::chrono::minutes(items.back());
        items.pop_back();
        if (!items.empty()) {
          value += std::chrono::hours(items.back());
        }
      }
    }
  }

  friend std::ostream &operator<<(std::ostream &out, const Config &config) {
    return out << config.ini;
  }
};

inline Config config;

#endif // CONFIG_H
