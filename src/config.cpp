/**
 * @file     config.cpp
 *
 * @brief    Handles everything regarding the configuration file like reading,
 *           parsing and verifying.
 *
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <regex>
#include <sstream>

#include "dmidecode_parser.h"
#include "logging.h"
#include "operators.h"

#include "config.h"

#define NAMEOF(name) #name
#define GET_VALUE(section, value) get_value(section, NAMEOF(value), value)
#define GET_VALUES(section, values) get_values(section, NAMEOF(values), values)
#define GET_VALUES_PARSE(section, values, parse)                               \
  get_values(section, NAMEOF(values), values, parse)

/**
 * @brief    Reads the values from the configuration.
 *
 */
void Config::read_values() {
  // dram_layout
  get_values("dram_layout", "row_masks", dram_layout.row_masks);
  get_values("dram_layout", "col_masks", dram_layout.col_masks);
  get_values("dram_layout", "functions", dram_layout.h_fns);

  // memory
  GET_VALUE("memory", alloc_page_size);
  GET_VALUE("memory", page_allocation_file);
  GET_VALUE("memory", use_free_memory);
  GET_VALUE("memory", allocate_percentage);
  GET_VALUE("memory", memory_size);
  GET_VALUE("memory", hugepage_count);

#ifdef USE_ASMJIT
  // blacksmith
  GET_VALUES("blacksmith",
             hammer_order); // before hammer_pattern, as it is used to determine
                            // the number of aggressors when "blacksmith" is
                            // specified as hammer_algorithm
  GET_VALUE("blacksmith", num_aggs_for_sync);
  GET_VALUE("blacksmith", total_num_activations);
  GET_VALUE("blacksmith", flushing);
  GET_VALUE("blacksmith", fencing);
#endif // USE_ASMJIT

  // hammer
  GET_VALUE("hammer", experiment_repetitions);
  GET_VALUE("hammer", threshold);
  GET_VALUE("hammer", hammer_count);
  GET_VALUE("hammer", aggressor_rows);
  GET_VALUE("hammer", memory_allocator);
  GET_VALUE("hammer", iter_algorithm);
  GET_VALUES("hammer", banks);
  GET_VALUE("hammer", row_padding);
  GET_VALUE("hammer",
            hammer_algorithm); // before hammer_pattern, cause if specified as
                               // "blacksmith" it is used in conjuction with
                               // determining the number of aggressor rows to
                               // use for the hammer pattern
  GET_VALUE("hammer",
            random_pattern_area); // before hammer_pattern, as it is used by the
                                  // hammer_pattern if a 'x' is specified in the
                                  // hammer_pattern
  get_value(
      "hammer", NAMEOF(hammer_pattern), hammer_pattern,
      std::function<HammerPattern(std::string)>([this](const std::string &s) {
        auto pattern = std::regex_replace(s, std::regex("0"), "v");
        pattern = std::regex_replace(pattern, std::regex("1"), "a");
        return HammerPattern(pattern, aggressor_rows);
      }));
  GET_VALUE("hammer", nop_count);
  GET_VALUES_PARSE("hammer", victim_init, parse_init_pattern);
  GET_VALUES_PARSE("hammer", aggressor_init, parse_init_pattern);
  GET_VALUE("hammer", test_min_rows);
  GET_VALUE("hammer", test_max_rows);
  GET_VALUE("hammer", test_first_row);
  GET_VALUE("hammer", test_last_row);
  GET_VALUE("hammer", test_max_time);

  // temperature
  GET_VALUE("temperature", device);
  GET_VALUES("temperature", target_temps);
  GET_VALUE("temperature", interval);
  GET_VALUE("temperature", timeout);

#ifdef USE_DB
  // db
  get_value("db", "filepath", db_filepath);
  GET_VALUES("db.configs", dimms);

  if (ini.contains("db.dimm_ids"))
    dimm_ids = ini["db.dimm_ids"];

  if (ini.contains("db.bios_settings"))
    bios_settings = ini["db.bios_settings"];

  get_value("db.experiments", "comment", experiment_comment);
#endif
}

/**
 * @brief    Verifies the configured values.
 *
 */
void Config::verify_values() {
  // dram_layout.row_masks
  if (!verify_masks(dram_layout.row_masks))
    log_error_and_exit("All 1-bits in row mask must be consecutive - use "
                       "multiple row masks for non-consecutive 1-bits");

  // dram_layout.col_masks
  if (!verify_masks(dram_layout.col_masks))
    log_error_and_exit("All 1-bits in column mask must be consecutive - use "
                       "multiple column masks for non-consecutive 1-bits");

  // banks
  uint64_t banks_cnt = config.dram_layout.get_banks_cnt();
  if (banks.empty()) {
    banks.resize(banks_cnt);
    std::iota(banks.begin(), banks.end(), 0);
  } else if (std::any_of(banks.begin(), banks.end(), [banks_cnt](const auto b) {
               return b >= banks_cnt;
             })) {
    log_error_and_exit("Specified a bank that is out of range [0, ",
                       banks_cnt - 1, "]");
  }

  // test_max_rows
  const uint64_t test_max_rows_lb = test_min_rows + row_padding * 2;
  if (test_max_rows > 0 && test_max_rows < test_max_rows_lb) {
    test_max_rows = test_max_rows_lb;
    log_warn("Changed value of test_max_rows to minimum allowed value ",
             test_max_rows, " (test_min_rows + 2 * row_padding)");
  }

  // hammer_pattern
  if (hammer_pattern.empty())
    hammer_pattern =
        HammerPattern(hammer_pattern.get_description(), aggressor_rows);

  // aggressor_init
  if (aggressor_init.empty()) {
    aggressor_init.resize(victim_init.size());
    std::transform(victim_init.begin(), victim_init.end(),
                   aggressor_init.begin(), [](uint64_t v) { return ~v; });
  }

  // victim_init
  if (victim_init.size() != aggressor_init.size()) {
    log_error_and_exit(NAMEOF(victim_init), " and ", NAMEOF(aggressor_init),
                       " must have same number of items");
  }

#ifdef USE_DB
  // dimm_ids
  if (!dimm_ids.empty()) {
    std::vector<std::string> detected_dimms;
    if (DMIDecodeParser::get_dimms(dimm_ids, detected_dimms)) {
      config.dimms = detected_dimms;
      log_info("Detected DIMMs: ", config.dimms);
    } else {
      log_warn("Defaulting to configured DIMMs: ", config.dimms);
    }
  }
#endif
}

/**
 * @brief    Reads the configuration file.
 *
 * @param    filename            Configuration filename.
 * @return   true                On successful read.
 * @return   false               On failed read.
 */
bool Config::read(const std::string &filename) {
  log_info("Parsing configuration file '", filename, "'");

  std::ifstream file(filename);

  if (!file.good()) {
    log_warn("Could not read configuration file '", filename,
             "', proceeding with default configuration");

    // initialize config.banks so all banks are tested
    uint64_t banks_cnt = config.dram_layout.get_banks_cnt();
    banks.resize(banks_cnt);
    std::iota(banks.begin(), banks.end(), 0);

    // initialize hammer pattern with default number of aggressors
    hammer_pattern =
        HammerPattern(hammer_pattern.get_description(), aggressor_rows);

    return false;
  }

  std::stringstream ss;
  ss << file.rdbuf();
  parse(ss);
  file.close();

  read_values();
  verify_values();

  return true;
}

/**
 * @brief    Parses the configuration file.
 *
 * @param    in                  Input stream of the configuration file.
 */
void Config::parse(std::istream &in) {
  static const std::regex comment_regex(R"([;#].*)");
  static const std::regex section_regex(R"(\[([^\]]+)\])");
  static const std::regex value_regex(R"(([^\s=]+)=(.*))");
  std::string current_section;
  std::smatch pieces;
  for (std::string line; std::getline(in, line);) {
    if (line.empty() || std::regex_match(line, pieces, comment_regex)) {
      // skip comment lines and blank lines
    } else if (std::regex_match(line, pieces, section_regex)) {
      if (pieces.size() == 2) { // exactly one match
        current_section = pieces[1].str();
      }
    } else if (std::regex_match(line, pieces, value_regex)) {
      if (pieces.size() == 3) { // exactly enough matches
        ini[current_section][pieces[1].str()] = pieces[2].str();
      }
    }
  }
}

/**
 * @brief    Verifies the row and column masks.
 *
 * @param    masks               The mask to verify.
 * @return   true                If it is a plausible mask.
 * @return   false               If it is not a plausible mask.
 */
bool Config::verify_masks(const std::vector<uint64_t> masks) {
  for (const auto &mask : masks) {
    const int leading_zeros = count_leading_zero_bits(mask);
    const int trailing_zeros = count_trailing_zero_bits(mask);
    const int ones = count_one_bits(mask);
    if (leading_zeros + ones + trailing_zeros != sizeof(uint64_t) * 8)
      return false;
  }
  return true;
}

/**
 * @brief    Checks for a correct initialization pattern and converts it from
 *           string to uint64_t.
 *
 * @param    s                   The initialization pattern as string.
 * @return   uint64_t            The initialization pattern as int.
 */
uint64_t Config::parse_init_pattern(std::string s) {
  std::string prefix = s.substr(0, 2);
  s.erase(0, 2);

  std::size_t orig_length = s.size();
  if (orig_length == 0 || ((orig_length & (orig_length - 1)) != 0)) {
    log_warn("Length of initialization pattern is not a power of two");
  }

  std::size_t base = 0;
  std::size_t length = 0;

  if (prefix == "0x") {
    base = 16;
    length = 16;
  } else if (prefix == "0b") {
    base = 2;
    length = 64;
  } else {
    log_error_and_exit(
        "Initialization pattern must be binary (0b) or hexadecimal (0x)");
  }

  if (orig_length > length) {
    log_warn("Maximum length of initialization pattern is ", length,
             ", truncating pattern");
  }

  std::ostringstream ss;
  std::size_t rep = length / orig_length + (length % orig_length != 0);
  for (std::size_t i = 0; i < rep; ++i)
    ss << s;

  return std::stoull(ss.str().substr(0, length), nullptr, base);
}

/**
 * @brief    Gets the value of a given section and key as a string.
 *
 * @param    section             The configuration section.
 * @param    key                 The configuration key.
 * @param    value               The configured value.
 */
void Config::get_value(const std::string &section, const std::string &key,
                       std::string &value) const {
  if (ini.contains(section) && ini.at(section).contains(key))
    value = ini.at(section).at(key);
}

/**
 * @brief    Gets the value of a given section and key as a uint64_t.
 *
 * @param    section             The configuration section.
 * @param    key                 The configuration key.
 * @param    value               The configured value.
 */
void Config::get_value(const std::string &section, const std::string &key,
                       uint64_t &value) const {
  if (ini.contains(section) && ini.at(section).contains(key))
    value = std::stoull(ini.at(section).at(key), nullptr, 0);
}

/**
 * @brief    Gets multiple comma-separated values of a given section and key as
 *           uint64_t.
 *
 * @param    section             The configuration section.
 * @param    key                 The configuration key.
 * @param    values              The configured values.
 * @param    parse               The method that should be used for parsing.
 */
void Config::get_values(
    const std::string &section, const std::string &key,
    std::vector<uint64_t> &values,
    const std::function<uint64_t(std::string)> &parse) const {
  if (ini.contains(section) && ini.at(section).contains(key)) {
    std::string str = ini.at(section).at(key);

    std::vector<std::string> cont;
    split(str, cont, ',');

    values.resize(cont.size());
    std::transform(cont.begin(), cont.end(), values.begin(), parse);
  }
}

/**
 * @brief    Gets multiple values of a given section and key as int64_t.
 *
 * Note that this method is a copy of the above method, cause the function
 * template partial specialisation does not work.
 *
 * @param    section             The configuration section.
 * @param    key                 The configuration key.
 * @param    values              The configured values.
 * @param    parse               The method that should be used for parsing.
 */
void Config::get_values(
    const std::string &section, const std::string &key,
    std::vector<int64_t> &values,
    const std::function<int64_t(std::string)> &parse) const {
  if (ini.contains(section) && ini.at(section).contains(key)) {
    std::string str = ini.at(section).at(key);

    std::vector<std::string> cont;
    split(str, cont, ',');

    values.resize(cont.size());
    std::transform(cont.begin(), cont.end(), values.begin(), parse);
  }
}
