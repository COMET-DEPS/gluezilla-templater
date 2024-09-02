/**
 * @file     tester.cpp
 *
 * @brief    The main file of gluezilla-templater. Used for iterating over and
 *           finding bit flips in a given memory area.
 *
 */

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>

#include <sys/sysinfo.h>

#include "config.h"
#include "contiguous_flip_finder.h"
#include "db.h"
#include "dram_address.h"
#include "flip_finder.h"
#include "info.h"
#include "logging.h"
#include "noncontiguous_flip_finder.h"
#include "operators.h"
#include "phys_page_finder.h"
#include "version.h"

/**
 * @brief    Creates the database, processes the configuration and starts either
 *           the contiguous or noncontiguous flip finder.
 *
 * @param    hostname            The hostname of the system as a default for the
 *                               database name.
 * @param    page_finder         The Physical Page Finder object.
 */
void process_config([[maybe_unused]] const std::string &hostname,
                    const PhysPageFinder &page_finder) {
  log_info("Configuration: ", config);

#ifdef USE_DB
  std::filesystem::path db_filepath = config.db_filepath.empty()
                                          ? "data/" + hostname + ".db"
                                          : config.db_filepath;
  if (db_filepath.has_parent_path()) {
    std::filesystem::create_directories(db_filepath.parent_path());
  }

  db = std::make_unique<DB>(db_filepath);
  db->load_or_insert_config(hostname, config.dimms, config.bios_settings,
                            config.dram_layout);
#endif

  const std::map<std::string, std::function<void(const PhysPageFinder &)>>
      find_flips{
        { "contiguous",
          [](auto... p) { ContiguousFlipFinder(p...).find_flips(); } },
        { "noncontiguous",
          [](auto... p) { NoncontiguousFlipFinder(p...).find_flips(); } }
      };

  find_flips.at(config.memory_allocator)(page_finder);

#ifdef USE_DB
  db.reset();
#endif
}

/**
 * @brief    Retrieves information about the system, initializes objects and
 *           handles the configuration files.
 *
 * @param    argc                Argument count.
 * @param    argv                Arguments.
 * @return   int                 0 on success.
 */
int main(int argc, char *argv[]) {

  const struct option longopts[] = { { "help", 0, 0, 'h' } };

  int index;
  int iarg = 0;

  while (iarg != -1) {
    iarg = getopt_long(argc, argv, "h", longopts, &index);

    switch (iarg) {
    case 'h':
      std::cout << "./bin/tester - execute gluezilla-templater\n" << std::endl;
      std::cout << "Must be executed as root (sudo)!\n" << std::endl;
      std::cout << std::setw(10) << std::left << "usage:"
                << "./bin/tester -h | --help\n"
                << std::endl;
      std::cout << std::setw(10) << std::left << "usage:"
                << "sudo ./bin/tester [config file]" << std::endl;
      std::cout << std::setw(10) << std::left << "example:"
                << "sudo ./bin/tester config.ini\n"
                << std::endl;
      std::cout << std::setw(10) << std::left << ""
                << "'config.ini' is the DEFAULT config file if left empty!"
                << std::endl;
      std::cout << std::setw(10) << std::left << ""
                << "A configuration template is available in the projects root "
                   "directory as 'default-config.ini'.\n"
                << std::endl;
      std::cout
          << std::setw(10) << std::left << "usage:"
          << "sudo ./bin/tester [base config file] [config file 1] [config "
             "file 2] [config file ...]"
          << std::endl;
      std::cout << std::setw(10) << std::left << "example:"
                << "sudo ./bin/tester base-config.ini config1.ini config2.ini"
                << std::endl;
      std::cout << std::setw(10) << std::left << "example:"
                << "sudo ./bin/tester configs/*\n"
                << std::endl;
      std::cout << std::setw(10) << std::left << ""
                << "The alphabetically first file is used as the base config."
                << std::endl;
      std::cout << std::setw(10) << std::left << ""
                << "Configs afterwards must only contain the changed setting."
                << std::endl;
      std::cout << std::setw(10) << std::left << ""
                << "Careful! Base config does not count as the first config to "
                   "use for hammering."
                << std::endl;

      return EXIT_SUCCESS;
    }
  }

  std::cout << std::fixed << std::setprecision(2) << std::boolalpha
            << std::showbase;

  log_info("Application name/version: ", argv[0], " ", GIT_NAME);
  log_info("Kernel version: ", get_kernel_version());
  log_info("OS release: ", read_os_release());

  const std::string hostname = get_hostname();
  log_info("Hostname: ", hostname);

  const std::string config_filename = argc > 1 ? argv[1] : "config.ini";
  config.read(config_filename);

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

  // PhysPageFinder allocates free or configured memory (see config.h)
  const PhysPageFinder page_finder;

  print_pageinfo(page_finder, phys_pages, available_phys_pages,
                 config.page_allocation_file);

  if (argc <= 2) {
    process_config(hostname, page_finder);
  } else {
    Config base_config(config);

    log_info("Using '", config_filename, "' as base configuration");
    log_warn("Only the base configuration is considered for memory allocation");

    for (int i = 2; i < argc; ++i) {
      config = base_config; // use base_config as default
      config.read(argv[i]);
      process_config(hostname, page_finder);
    }
  }

  return EXIT_SUCCESS;
}
