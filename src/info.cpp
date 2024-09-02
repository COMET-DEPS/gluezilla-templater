/**
 * @file     info.cpp
 *
 * @brief    Contains everything regarding the system configuration, like
 *           hostname, kernel version, ...
 *
 */

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <limits.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "logging.h"
#include "operators.h"

#include "info.h"

/**
 * @brief    Gets the hostname.
 *
 * @return   std::string         The hostname.
 */
std::string get_hostname() {
  char hostname[HOST_NAME_MAX];
  gethostname(hostname, HOST_NAME_MAX);
  return hostname;
}

/**
 * @brief    Gets the kernel version.
 *
 * @return   std::string         The kernel version.
 */
std::string get_kernel_version() {
  struct utsname name;
  return uname(&name) == EXIT_SUCCESS ? std::string(name.sysname) + " " +
                                            name.release + " " + name.machine
                                      : "";
}

/**
 * @brief    Reads the os release.
 *
 * @param    property            The name of the property containing the os
 *                               release.
 * @return   std::string         The os release.
 */
std::string read_os_release(const std::string &property) {
  std::ifstream infile("/etc/os-release");
  std::string line;
  while (std::getline(infile, line)) {
    if (line.starts_with(property)) {
      std::string result = line.substr(property.length() + 1);
      result.erase(std::remove(result.begin(), result.end(), '\"'),
                   result.end());
      return result;
    }
  }

  return "";
}

/**
 * @brief    Reads and prints information about the system.
 *
 * @param    sys_info            The system information.
 * @param    phys_pages          The total count of physical pages.
 * @param    available_phys_pages
 *                               The available physical pages.
 * @return   true                On successful read.
 * @return   false               On failure.
 */
bool read_sysinfo(struct sysinfo &sys_info, long &phys_pages,
                  long &available_phys_pages) {
  if (sysinfo(&sys_info) != EXIT_SUCCESS)
    return false;

  phys_pages = get_phys_pages();
  available_phys_pages = get_avphys_pages();

  log_info("Memory usage:");
  log_info("         memory [bytes]", std::string(8, ' '), "physical pages");
  log_info("  total", std::setw(13), sys_info.totalram, " (", std::setw(2),
           (sys_info.totalram >> 30), " GiB)", std::setw(9), phys_pages);
  log_info("  free ", std::setw(13), sys_info.freeram, " (", std::setw(2),
           (sys_info.freeram >> 30), " GiB)", std::setw(9),
           available_phys_pages, " (",
           available_phys_pages * 100.0 / phys_pages, " %)");

  return true;
}

/**
 * @brief    Prints information about pages.
 *
 * @param    finder              The physical page finder.
 * @param    phys_pages          The total count of physical pages.
 * @param    available_phys_pages
 *                               The available physical pages.
 * @param    filename            The filename where page allocation should be
 *                               saved.
 */
void print_pageinfo(const PhysPageFinder &finder, const long phys_pages,
                    const long available_phys_pages,
                    const std::string &filename) {
  auto alloc_pages = finder.size();
  auto missing_pages = phys_pages - alloc_pages;

  {
    ios_fmt_saver ifs(std::cout);
    log_info("Pages allocated:", std::right, std::setw(9), alloc_pages, " (",
             alloc_pages * 100.0 / available_phys_pages, " % of free pages)");
    log_info("Pages missing:", std::setw(11), missing_pages, std::left, " (",
             missing_pages * 100.0 / phys_pages, " % of total pages)");
  }

  if (filename.empty())
    return;

  const std::size_t max_pages = 36_GiB / page_size; // = total memory/page size
  std::bitset<max_pages> pages;

  for (std::size_t p = 0; p < pages.size(); ++p) {
    if (finder.contains(p))
      pages.flip(p);
  }

  log_info("Save page allocation data to '", filename, "'...");
  std::ofstream output_file(filename);
  output_file << pages;
}
