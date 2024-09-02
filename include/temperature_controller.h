#ifndef TEMPERATURE_CONTROLLER_H
#define TEMPERATURE_CONTROLLER_H

#include <cstdint>
#include <cstdlib>
#include <string>

#include "config.h"

class TemperatureController {
  int64_t target_temperature = 0;
  int64_t serial_fd = 0;
  char ignore_char;

  std::string read_line() const;
  std::string read_line(char *read_buf, std::size_t buf_size) const;
  static void write_string(uint64_t serial_dev, const std::string &send_buf);
  static void configure_serial_port(int64_t serial_dev);

public:
  TemperatureController(char ignore_char = '#') : ignore_char(ignore_char) {}

  bool connect();
  void set_target_temperature(int64_t target_temperature);
  int64_t get_target_temperature() const;
  int64_t get_actual_temperature() const;
};

#endif // TEMPERATURE_CONTROLLER_H
