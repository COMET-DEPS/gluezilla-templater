/**
 * @file     dmidecode_parser.cpp
 *
 * @brief    Handles the automatic detection of DIMM modules.
 *
 */

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <regex>
#include <stdexcept>

#include "logging.h"

#include "dmidecode_parser.h"

/**
 * @brief    Handles the execution of a command.
 *
 * @param    cmd                 The command to execute.
 * @return   std::string         The output of the command.
 */
std::string DMIDecodeParser::exec(const std::string &cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"),
                                                pclose);

  if (!pipe)
    throw std::runtime_error("popen() failed!");

  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }

  return result;
}

/**
 * @brief    Parses the dmidecode output for DIMM modules.
 *
 * @param    dmidecode_output    The output of the dmidecode command.
 * @return   std::vector<std::tuple<int, std::string>>
 *                               The present DIMM modules with serial number.
 */
std::vector<std::tuple<int, std::string>>
DMIDecodeParser::parse(const std::string &dmidecode_output) {
  std::vector<std::tuple<int, std::string>> devices;
  std::smatch matches;
  std::regex regex = std::regex(
      R"(Memory Device[\s\S]*?\tLocator: ([\S\ ]*)[\s\S]*?\tSerial Number: ([\S\ ]*))",
      std::regex::ECMAScript);

  static const std::map<std::string, int> locator_slot{ // for SMBIOS 2
                                                        { "DIMM1", 0 },
                                                        { "DIMM2", 1 },
                                                        { "DIMM3", 2 },
                                                        { "DIMM4", 3 },
                                                        // for SMBIOS 3.0
                                                        { "ChannelA-DIMM2", 0 },
                                                        { "ChannelB-DIMM2", 1 },
                                                        { "ChannelA-DIMM1", 2 },
                                                        { "ChannelB-DIMM1", 3 },
                                                        // for SMBIOS 3.3
                                                        { "DIMM_B2", 0 },
                                                        { "DIMM_A2", 1 },
                                                        { "DIMM_B1", 2 },
                                                        { "DIMM_A1", 3 }
  };

  std::string::const_iterator it = dmidecode_output.cbegin();
  while (std::regex_search(it, dmidecode_output.cend(), matches, regex)) {
    if (matches.size() != 3) {
      log_warn("Could not parse dmidecode entry");
    } else {
      if (matches[2] != "[Empty]" /* for DDR3 */ &&
          matches[2] != "Not Specified" /* for DDR4 */) {
        if (!locator_slot.contains(matches[1])) {
          log_warn("Could not determine DIMM slot: ", matches[1]);
        } else {
          devices.emplace_back(locator_slot.at(matches[1]), matches[2]);
        }
      }
    }

    it = matches[0].second;
  }

  return devices;
}

/**
 * @brief    Executes the dmidecode command to get information about memory.
 *
 * @return   std::vector<std::tuple<int, std::string>>
 *                               The present DIMM modules with serial number.
 */
std::vector<std::tuple<int, std::string>>
DMIDecodeParser::get_memory_devices() {
  return parse(exec("dmidecode --type memory"));
}

/**
 * @brief    Parses for already known serial numbers.
 *
 * @param    raw_serial_number   The raw serial number.
 * @param    parsed_serial_number
 *                               The parsed serial number.
 * @return   true                If serial number is known.
 * @return   false               If serial number is not known or has a wrong
 *                               format.
 */
bool DMIDecodeParser::parse_serial_number(const std::string &raw_serial_number,
                                          std::string &parsed_serial_number) {
  if (raw_serial_number == "Unknown" ||
      std::regex_match(raw_serial_number, std::regex("SerNum\\d")) ||
      std::stoull(raw_serial_number, 0, 16) == 0) {
    log_warn("Invalid serial number: ", raw_serial_number);
    return false;
  }

  if (raw_serial_number.size() % 3 != 0) {
    if (raw_serial_number.size() % 2 != 0) {
      log_warn("Invalid serial number length: ", raw_serial_number);
      return false;
    }

    // should be a DDR4 serial number that is already correctly formatted
    parsed_serial_number = "0x" + raw_serial_number;
    return true;
  }

  // reformat DDR3 serial number (reverse byte order, remove zeros)
  parsed_serial_number = "0x";
  int i = raw_serial_number.size() - 1;

  while (i >= 0) {
    parsed_serial_number += raw_serial_number[i - 1];
    parsed_serial_number += raw_serial_number[i];
    if (raw_serial_number[i - 2] != '0') {
      log_warn("Invalid serial number format: ", raw_serial_number);
      return false;
    }
    i -= 3;
  }

  return true;
}

/**
 * @brief    Automatic detection of DIMM modules.
 *
 * @param    dimm_ids            The known DIMM modules.
 * @param    dimms               The present DIMM modules.
 * @return   true                On successful automatic detection.
 * @return   false               On failed automatic detection.
 */
bool DMIDecodeParser::get_dimms(std::map<std::string, std::string> dimm_ids,
                                std::vector<std::string> &dimms) {
  const auto devices = get_memory_devices();
  if (devices.size() == 0)
    log_error("Automatic detection of DIMMs failed");

  dimms.clear();
  dimms.resize(std::max(4ul, devices.size()));

  for (const auto &[slot, raw_serial] : devices) {
    std::string serial;
    if (!parse_serial_number(raw_serial, serial))
      return false;

    if (!dimm_ids.contains(serial)) {
      log_warn("ID for DIMM with serial number ", serial,
               " is missing from configuration");
      return false;
    }

    dimms[slot] = dimm_ids[serial];
  }

  return true;
}
