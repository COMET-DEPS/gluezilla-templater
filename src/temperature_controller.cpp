/**
 * @file     temperature_controller.cpp
 *
 * @brief    Contains everything regarding connecting, setting the
 *           temperature and getting the actual temperature of the used
 *           temperature controller.
 *
 * The temperature controller class is used for testing with the climate chamber
 * or heating pads built by FH. Therefore, a temperature controller gets
 * connected to the computer per USB. The usage is as follows: Connect to the
 * serial device (USB), send a target temperature and check the actual
 * temperature sent by the serial device. The configuration is done inside the
 * temperature section of the config.ini file.
 *
 * The important setting for this class is the device setting. Connecting the
 * first USB device usually results in the device /dev/ttyUSB0 on Ubuntu.
 *
 */

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>
#include <limits>
#include <regex>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termio.h>
#include <unistd.h>

#include "config.h"
#include "logging.h"

#include "temperature_controller.h"

/**
 * @brief    Initiate listening on the serial device.
 *
 * @return   std::string         My Return doc
 */
std::string TemperatureController::read_line() const {
  const size_t buf_size = 64;
  char local_read_buffer[buf_size];
  return read_line(local_read_buffer, buf_size);
}

/**
 * @brief    Listens on output from the serial device until a string gets sent.
 *
 * This function is used recursively.
 *
 * @param    read_buf            The read buffer.
 * @param    buf_size            The buffer size.
 * @return   std::string         The sent string.
 */
std::string TemperatureController::read_line(char *read_buf,
                                             std::size_t buf_size) const {
  std::deque<char> buffer;
  auto line_end = std::find(buffer.cbegin(), buffer.cend(), '\n');

  // Read until line ending is found
  while (line_end == buffer.cend()) {
    int n = read(serial_fd, read_buf, buf_size - 1);
    if (n < 0) {
      log_error_and_exit("Received invalid reply from temperature controller");
    }
    read_buf[n] = '\0';

    auto pref_end = buffer.cend();
    std::copy(read_buf, read_buf + n, std::back_inserter(buffer));
    line_end = std::find(pref_end, buffer.cend(), '\n');
  }

  // Copy line to buffer
  std::string line;
  line.reserve(std::distance(buffer.cbegin(), line_end));
  std::copy(buffer.cbegin(), line_end, std::back_inserter(line));

  // Delete line from buffer
  buffer.erase(buffer.begin(), ++line_end);

  // If line is ignored, read another line
  if (line.size() == 0) {
    return read_line(read_buf, buf_size);
  } else if (line.front() == ignore_char) {
    log_trace(line);
    return read_line(read_buf, buf_size);
  } else {
    return line;
  }
}

/**
 * @brief    Configures the command line parameters for the serial device.
 *
 * OPOST (Postprocessing) must be disabled. Otherwise the microcontroller does
 * not receive all commands.
 *
 * @param    serial_dev          The serial device.
 */
void TemperatureController::configure_serial_port(int64_t serial_dev) {
  struct termios tio;
  tcgetattr(serial_dev, &tio);
  cfsetspeed(&tio, B115200);

  tio.c_cflag &= ~(CSTOPB | PARENB);
  tio.c_cflag |= CS8;
  tio.c_lflag &= ~(ECHO);
  tio.c_iflag |= IXOFF;
  tio.c_oflag &= ~(OPOST);
  tio.c_cc[VMIN] = 1;
  tio.c_cc[VTIME] = 0;

  if (tcsetattr(serial_dev, TCSANOW, &tio) < 0) {
    log_error_and_exit("Could not configure device", config.device);
  }
}

/**
 * @brief    Writes a string to the specified serial device.
 *
 * @param    serial_dev          The serial device.
 * @param    send_buf            The string.
 */
void TemperatureController::write_string(uint64_t serial_dev,
                                         const std::string &send_buf) {
  log_trace("Sending command '",
            std::regex_replace(send_buf, std::regex("\n"), "\\n"),
            "' to device");

  uint64_t sent = 0;
  do {
    auto n = write(serial_dev, send_buf.c_str() + sent, send_buf.size() - sent);
    if (n < 0) {
      log_error_and_exit("Could not write command to device");
    }
    sent += n;
  } while (sent < send_buf.size());
}

/**
 * @brief    Connects to the temperature controller.
 *
 * @return   true                On successful connection.
 * @return   false               On failed connection.
 */
bool TemperatureController::connect() {
  serial_fd = open(config.device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
  if (serial_fd < 0)
    return false;
  configure_serial_port(serial_fd);
  return true;
}

/**
 * @brief    Sets the target temperature of the temperature controller.
 *
 * @param    target_temperature  The target temperature.
 */
void TemperatureController::set_target_temperature(int64_t target_temperature) {
  this->target_temperature = target_temperature;
  std::stringstream send_stream;
  send_stream << "setTargetTemp" << ';' << target_temperature << '\n';
  write_string(serial_fd, send_stream.str());
  log_info("Using target temperature ", target_temperature, " °C");
}

/**
 * @brief    Gets the set target temperature.
 *
 * @return   int64_t             The set target temperature.
 */
int64_t TemperatureController::get_target_temperature() const {
  return target_temperature;
}

/**
 * @brief    Gets temperature reading from temperature controller.
 *
 * @return   int64_t             The actual temperature.
 */
int64_t TemperatureController::get_actual_temperature() const {
  write_string(serial_fd, "getActualTemp;\n");
  auto line = read_line();

  try {
    auto temp = static_cast<int64_t>(std::floor(std::stod(line, nullptr)));
    log_debug("Current temperature: ", temp, " °C");
    return temp;
  } catch (const std::invalid_argument &) {
    log_error("Could not parse temperature: ", line);
    return std::numeric_limits<int64_t>::min();
  }
}
