#ifndef HAMMER_PATTERN_H
#define HAMMER_PATTERN_H

#include <cstdint>
#include <string>
#include <vector>

// "dynamic bitset" encoding a pattern of victims (0s) and aggressors (1s)
class HammerPattern : public std::vector<bool> {
  // string representation of the hammer pattern, as in configuration file
  std::string description;

  std::vector<uint64_t>
  generate_random_fill_up(uint64_t needed_random_area,
                          uint64_t needed_random_value_count) const;
  void create_pattern(uint32_t aggressor_rows);
  uint64_t count_char_in_description(char8_t character) const;
  bool validate_description() const;

public:
  // initializes only member description
  HammerPattern(const std::string &description) : description(description) {}
  // initializes member description and parses it considering aggressor_rows,
  // may increase the argument of aggressor_rows so that aggressor_rows is a
  // multiple of the aggressors (1s) in description
  HammerPattern(const std::string &description, uint32_t &aggressors_rows);

  std::string get_description() {
    return description;
  };
};

#endif // HAMMER_PATTERN_H
