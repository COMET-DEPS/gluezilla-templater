#ifndef DMIDECODE_PARSER_H
#define DMIDECODE_PARSER_H

#include <map>
#include <string>
#include <tuple>
#include <vector>

class DMIDecodeParser {
  static std::string exec(const std::string& cmd);
  static std::vector<std::tuple<int, std::string>>
  parse(const std::string &dmidecode_output);

public:
  static std::vector<std::tuple<int, std::string>>
  get_memory_devices();
  static bool parse_serial_number(const std::string &raw_serial_number,
                                  std::string &parsed_serial_number);
  static bool get_dimms(std::map<std::string, std::string> dimm_ids,
                        std::vector<std::string> &dimms);
};

#endif // DMIDECODE_PARSER_H
