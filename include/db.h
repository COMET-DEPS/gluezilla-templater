#ifndef USE_DB
#define DB_H
#endif

#ifndef DB_H
#define DB_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "dram_address.h"
#include "dram_layout.h"

class DB {
  const int64_t schema_version = 4;

  std::string filename;

  sqlite3 *db = nullptr;
  sqlite3_stmt *stmt_start_experiment = nullptr;
  sqlite3_stmt *stmt_end_experiment = nullptr;
  sqlite3_stmt *stmt_insert_test = nullptr;
  sqlite3_stmt *stmt_insert_bitflip = nullptr;
  sqlite3_stmt *stmt_insert_aggressor = nullptr;

  int64_t config_id = 0;
  int64_t experiment_id = 0;
  int64_t test_id = 0;

  bool connect();
  int64_t get_schema_version() const;
  bool prepare_stmt(const std::string &sql, sqlite3_stmt *&stmt) const;
  bool create_schema() const;
  void close();

public:
  DB(const std::string &filename);
  virtual ~DB();

  bool begin_transaction() const;
  bool commit() const;

  int64_t
  load_or_insert_config(const std::string &hostname,
                        const std::vector<std::string> &dimms,
                        const std::map<std::string, std::string> &bios_settings,
                        const DRAMLayout &dram_layout);
  int64_t start_experiment(uint64_t aggressor_rows, uint64_t hammer_count,
                           int64_t target_temp,
                           const std::string &comment = "");
  bool end_experiment();
  int64_t insert_test(const std::vector<uint64_t> &aggressors,
                      uint64_t hammer_time, uint64_t victim_init,
                      uint64_t aggressor_init, int64_t actual_temp);
  int64_t insert_bitflip(uint64_t victim, uint8_t bit, uint8_t flipped_to);
};

inline std::unique_ptr<DB> db;

#endif // DB_H
