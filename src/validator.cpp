#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <sys/sysinfo.h>

#include "bit_flipper.h"
#include "config.h"
#include "info.h"
#include "logging.h"
#include "operators.h"
#include "phys_page_finder.h"
#include "version.h"

void read_addrfile(const std::string &filename,
                   std::vector<HammerAddrs> &addrs) {
  std::ifstream file(filename);
  std::string line;

  while (std::getline(file, line)) {
    std::vector<std::string> cont;
    split(line, cont, ',');

    HammerAddrs addr;
    // calculate start address of victim row
    addr.victims = { (std::stoull(cont.back(), nullptr, 16) / row_size) *
                     row_size };
    addr.aggs.resize(cont.size() - 2);
    std::transform(
        std::next(cont.begin()), std::prev(cont.end()), addr.aggs.begin(),
        [](const std::string &s) { return std::stoull(s, nullptr, 16); });
    addrs.push_back(addr);
  }
}

int main(int argc, char *argv[]) {
  log_info("Application name/version: ", argv[0], " ", GIT_NAME);

  if (argc <= 1) {
    log_error_and_exit("Usage: ", argv[0],
                       " addresses.txt [config.ini] [page_allocation.txt]");
  }

  config.read(argc > 2 ? argv[2] : "config.ini");

  std::cout << std::fixed << std::setprecision(2) << std::boolalpha;

  std::vector<HammerAddrs> addrs;

  read_addrfile(argv[1], addrs);

  struct sysinfo sys_info;
  long phys_pages = 0;
  long available_phys_pages = 0;

  if (read_sysinfo(sys_info, phys_pages, available_phys_pages)) {
    if (config.use_free_memory) {
      config.memory_size =
          static_cast<uint64_t>(sys_info.freeram * config.allocate_percentage);
    }
  } else {
    log_warn("Could not retrieve sysinfo");
  }

  const PhysPageFinder
      finder; // allocates free or configured memory (see config.h)
  print_pageinfo(finder, phys_pages, available_phys_pages,
                 argc > 3 ? argv[3] : "");

  for (const HammerAddrs &a : addrs) {
    BitFlipper flipper(a);

    if (flipper.find_pages(finder)) {
      log_info("Hammer ", a.aggs.size(), " aggressors...");
      flipper.hammer();
    } else {
      log_info("Could not find physical pages");
    }
  }

  return EXIT_SUCCESS;
}
