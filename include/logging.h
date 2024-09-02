#ifndef LOGGING_H
#define LOGGING_H

#include <cstdlib>
#include <iostream>
#include <string>

#include <unistd.h>

#include "operators.h"

#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_TRACE 5

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

struct TerminalColorChanger {
  static constexpr char const *fg_red_bold = "31;1";
  static constexpr char const *fg_green = "32";
  static constexpr char const *fg_yellow_bold = "33;1";
  static constexpr char const *fg_blue_bold = "34;1";
  static constexpr char const *reset = "0";

  static void set_out(char const *color = reset) {
#ifdef LOG_COLOR
    static bool is_terminal = isatty(1);
    if (is_terminal)
      printf("\033[%sm", color);
#endif
  }

  static void set_err(char const *color = reset) {
#ifdef LOG_COLOR
    static bool is_terminal = isatty(2);
    if (is_terminal)
      fprintf(stderr, "\033[%sm", color);
#endif
  }
};

template <typename... Args>
void log(std::ostream &out, const std::string &log_level, Args... args) {
  ios_fmt_saver ifs(out);
  out << now() << " [" << log_level << "] ";
  (out << ... << args) << '\n';
}

template <typename... Args>
void log_error(Args... args) {
#if LOG_LEVEL >= LOG_LEVEL_ERROR
  TerminalColorChanger::set_err(TerminalColorChanger::fg_red_bold);
  log(std::cerr, "ERROR", args...);
  TerminalColorChanger::set_err();
#endif
}

template <typename... Args>
void log_error_and_exit(Args... args) {
  log_error(args...);
  exit(EXIT_FAILURE);
}

template <typename... Args>
void log_warn(Args... args) {
#if LOG_LEVEL >= LOG_LEVEL_WARN
  TerminalColorChanger::set_err(TerminalColorChanger::fg_yellow_bold);
  log(std::cerr, "WARN", args...);
  TerminalColorChanger::set_err();
#endif
}

template <typename... Args>
void log_info(Args... args) {
#if LOG_LEVEL >= LOG_LEVEL_INFO
  log(std::cout, "INFO", args...);
  std::cout << std::flush; // std::cout is not flushed automatically
#endif
}

template <typename... Args>
void log_info_flip(Args... args) {
#if LOG_LEVEL >= LOG_LEVEL_INFO && LOG_FLIPS
  log(std::cout, "INFO", args...);
#endif
}

template <typename... Args>
void log_debug(Args... args) {
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
  TerminalColorChanger::set_out(TerminalColorChanger::fg_blue_bold);
  log(std::cout, "DEBUG", args...);
  TerminalColorChanger::set_out();
#endif
}

template <typename... Args>
void log_trace(Args... args) {
#if LOG_LEVEL >= LOG_LEVEL_TRACE
  TerminalColorChanger::set_out(TerminalColorChanger::fg_green);
  log(std::cout, "TRACE", args...);
  TerminalColorChanger::set_out();
#endif
}

#pragma GCC diagnostic pop

#endif // LOGGING_H
