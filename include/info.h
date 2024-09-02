#ifndef INFO_H
#define INFO_H

#include <string>

#include <sys/sysinfo.h>

#include "config.h"
#include "phys_page_finder.h"

std::string get_hostname();
std::string get_kernel_version();
std::string read_os_release(const std::string &property = "PRETTY_NAME");
bool read_sysinfo(struct sysinfo &sys_info, long &phys_pages,
                  long &available_phys_pages);

void print_pageinfo(const PhysPageFinder &finder, const long phys_pages,
                    const long available_phys_pages,
                    const std::string &filename = "");

#endif // INFO_H
