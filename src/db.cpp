/**
 * @file     db.cpp
 *
 * @brief    Contains everything regarding the used SQLite Database.
 *
 */

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "config.h"
#include "info.h"
#include "logging.h"
#include "version.h"

#include "db.h"

#define CHECK(fn)                                                              \
  {                                                                            \
    int return_code = fn;                                                      \
    if (return_code != SQLITE_OK && return_code != SQLITE_DONE &&              \
        return_code != SQLITE_ROW) {                                           \
      log(std::cerr, "SQLITE ERROR " + std::to_string(return_code), __FILE__,  \
          ":", __LINE__, ": ", sqlite3_errmsg(db));                            \
      return 0;                                                                \
    }                                                                          \
  }

/**
 * @brief    Initialization of the database object.
 *
 * @param    filename            The database filename.
 */
DB::DB(const std::string &filename) : filename(filename) {
  connect();
  create_schema();
}

/**
 * @brief    Destruction of the database object.
 *
 */
DB::~DB() {
  close();
}

/**
 * @brief    Opening the database connection.
 *
 * @return   true                On successful connection.
 * @return   false               On failed connection.
 */
bool DB::connect() {
  CHECK(sqlite3_open(filename.c_str(), &db));
  CHECK(
      sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr));
  return true;
}

bool DB::prepare_stmt(const std::string &sql, sqlite3_stmt *&stmt) const {
  if (stmt == nullptr) {
    CHECK(sqlite3_prepare_v2(db, sql.c_str(), sql.length(), &stmt, nullptr));
  } else {
    CHECK(sqlite3_reset(stmt));
    CHECK(sqlite3_clear_bindings(stmt));
  }

  return true;
}

/**
 * @brief    Creation of the database schemas.
 *
 * @return   true                On existing schemas or successful creation.
 * @return   false               On failure.
 */
bool DB::create_schema() const {
  int64_t existing_schema_version = get_schema_version();
  if (existing_schema_version == schema_version)
    return true;

  std::string sql;

  switch (existing_schema_version) {
  case 0:
    sql =
#include "create_tables.sql"
        ;
    break;
  case 1:
    sql =
#include "alter_tables_2.sql"
        ;
    [[fallthrough]];
  case 2:
    sql =
#include "alter_tables_3.sql"
        ;
    log_info("Upgrading database from schema version ", existing_schema_version,
             " to 3...");
    break;
  case INT64_MAX:
    log_info("Upgrading database from schema version ", existing_schema_version,
             " to ", schema_version, "...");
    break;
  default:
    log_error_and_exit("Expected schema version ", schema_version, ", ",
                       filename, " uses schema version ",
                       existing_schema_version);
  }

  sql = "BEGIN TRANSACTION;"
        "PRAGMA user_version = " +
        std::to_string(schema_version) + ";" + sql +
#include "create_views.sql"
        "COMMIT;";

  CHECK(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr));

  // restart required after upgrading from schema version 2 to 3
  if (existing_schema_version > 0 && existing_schema_version < 3) {
    log_info("Done - please restart application");
    exit(EXIT_FAILURE);
  }

  return true;
}

/**
 * @brief    Closing the database connection.
 *
 */
void DB::close() {
  sqlite3_finalize(stmt_start_experiment);
  stmt_start_experiment = nullptr;
  sqlite3_finalize(stmt_end_experiment);
  stmt_end_experiment = nullptr;
  sqlite3_finalize(stmt_insert_test);
  stmt_insert_test = nullptr;
  sqlite3_finalize(stmt_insert_bitflip);
  stmt_insert_bitflip = nullptr;
  sqlite3_finalize(stmt_insert_aggressor);
  stmt_insert_aggressor = nullptr;
  sqlite3_close(db);
  db = nullptr;
}

/**
 * @brief    Gets the current schema version of an existing database.
 *
 * @return   int64_t             The schema version.
 */
int64_t DB::get_schema_version() const {
  const char *sql = "PRAGMA user_version;";
  int64_t result = -1;

  sqlite3_exec(
      db, sql,
      [](void *ctx, int argc, char **argv, char **col_name) {
        (void)argc, (void)col_name; // unused parameters
        *static_cast<int64_t *>(ctx) = std::atoll(*argv);
        return 0;
      },
      &result, nullptr);

  return result;
}

/**
 * @brief    Begin transaction.
 *
 * @return   true                On success.
 * @return   false               On failure.
 */
bool DB::begin_transaction() const {
  CHECK(sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr));
  return true;
}

/**
 * @brief    Commit.
 *
 * @return   true                On success.
 * @return   false               On failure.
 */
bool DB::commit() const {
  CHECK(sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr));
  return true;
}

/**
 * @brief    Converts an uint64_t to a hex string.
 *
 * @param    value               Value to convert.
 * @param    width               Width of the hex value.
 * @return   std::string         The hex string.
 */
std::string to_hex_string(uint64_t value, int width = 0) {
  std::ostringstream ss;
  ss << std::hex << "0x" << std::setfill('0') << std::setw(width) << value;
  return ss.str();
}

/**
 * @brief    Converts uint64_t values to a hex string.
 *
 * @param    values              Values to convert.
 * @return   std::string         The hex string.
 */
std::string to_hex_string(const std::vector<uint64_t> &values) {
  if (values.size() == 1)
    return to_hex_string(values[0]);

  std::ostringstream ss;
  ss << std::showbase << std::hex << values;
  return ss.str();
}

/**
 * @brief    Compares two Maps for equality.
 *
 * @tparam   Map
 * @param    lhs                 Left hand side.
 * @param    rhs                 Right hand side.
 * @return   true                If equal.
 * @return   false               If not equal.
 */
template <typename Map>
bool map_compare(Map const &lhs, Map const &rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

/**
 * @brief    Loads or inserts the current system configuration.
 *
 * @param    hostname            The systems hostname.
 * @param    dimms               The DIMM modules.
 * @param    bios_settings       The configured BIOS settings.
 * @param    dram_layout         The DRAM layout (Mapping Functions, ...)
 * @return   int64_t             The config ID on success.
 */
int64_t DB::load_or_insert_config(
    const std::string &hostname, const std::vector<std::string> &dimms,
    const std::map<std::string, std::string> &bios_settings,
    const DRAMLayout &dram_layout) {
  std::vector<int64_t> possible_config_ids;

  const std::string row_masks = to_hex_string(dram_layout.row_masks);
  const std::string col_masks = to_hex_string(dram_layout.col_masks);

  std::vector<const char *> dimms_str;
  for (const auto &dimm : dimms)
    dimms_str.push_back(dimm.empty() ? nullptr : dimm.c_str());

  if (dimms.size() != 4) {
    dimms_str.resize(4, nullptr);
    log_warn("Database expected 4 DIMMs, got ", dimms.size());
  }

  const auto config_exists = [this, &hostname, &dimms_str, &row_masks,
                              &col_masks, &possible_config_ids]() -> int64_t {
    sqlite3_stmt *stmt = nullptr;
    const std::string sql =
        "SELECT id "
        "FROM configs "
        "WHERE hostname = ?1 AND "
        "  (dimm0 = ?2 OR (dimm0 IS NULL AND ?2 IS NULL)) AND "
        "  (dimm1 = ?3 OR (dimm1 IS NULL AND ?3 IS NULL)) AND "
        "  (dimm2 = ?4 OR (dimm2 IS NULL AND ?4 IS NULL)) AND "
        "  (dimm3 = ?5 OR (dimm3 IS NULL AND ?5 IS NULL)) AND "
        "  row_mask = ?6 AND col_mask = ?7;";
    if (!prepare_stmt(sql, stmt))
      return 0;

    CHECK(sqlite3_bind_text(stmt, 1, hostname.c_str(), -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 2, dimms_str[0], -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 3, dimms_str[1], -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 4, dimms_str[2], -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 5, dimms_str[3], -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 6, row_masks.c_str(), -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 7, col_masks.c_str(), -1, nullptr));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      possible_config_ids.push_back(sqlite3_column_int64(stmt, 0));
    }

    sqlite3_finalize(stmt);

    return !possible_config_ids.empty();
  };

  const auto mapping_functions_exist =
      [this, &dram_layout](int64_t config_id) -> int64_t {
    sqlite3_stmt *stmt = nullptr;
    const std::string sql = "SELECT function "
                            "FROM mapping_functions "
                            "WHERE config_id = ?;";
    if (!prepare_stmt(sql, stmt))
      return 0;

    CHECK(sqlite3_bind_int64(stmt, 1, config_id));
    std::vector<std::string> mapping_functions_db;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      mapping_functions_db.push_back(
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);

    std::vector<std::string> mapping_functions(dram_layout.h_fns.size());
    std::transform(dram_layout.h_fns.begin(), dram_layout.h_fns.end(),
                   mapping_functions.begin(),
                   [](uint64_t f) { return to_hex_string(f); });

    std::sort(mapping_functions.begin(), mapping_functions.end());
    std::sort(mapping_functions_db.begin(), mapping_functions_db.end());
    return mapping_functions == mapping_functions_db;
  };

  const auto bios_settings_exist =
      [this, &bios_settings](int64_t config_id) -> int64_t {
    sqlite3_stmt *stmt = nullptr;
    const std::string sql = "SELECT \"key\", value "
                            "FROM bios_settings "
                            "WHERE config_id = ?;";
    if (!prepare_stmt(sql, stmt))
      return 0;

    CHECK(sqlite3_bind_int64(stmt, 1, config_id));
    std::map<std::string, std::string> bios_settings_db;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      bios_settings_db[reinterpret_cast<const char *>(
          sqlite3_column_text(stmt, 0))] =
          reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
    }
    sqlite3_finalize(stmt);

    return map_compare(bios_settings, bios_settings_db);
  };

  const auto insert_config = [this, &hostname, &bios_settings, &dram_layout,
                              &row_masks, &col_masks, &dimms_str]() -> int64_t {
    sqlite3_stmt *stmt = nullptr;
    std::string sql = "INSERT INTO configs (hostname, dimm0, dimm1, dimm2, "
                      "  dimm3, row_mask, col_mask) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";
    if (!prepare_stmt(sql, stmt))
      return 0;

    CHECK(sqlite3_bind_text(stmt, 1, hostname.c_str(), -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 2, dimms_str[0], -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 3, dimms_str[1], -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 4, dimms_str[2], -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 5, dimms_str[3], -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 6, row_masks.c_str(), -1, nullptr));
    CHECK(sqlite3_bind_text(stmt, 7, col_masks.c_str(), -1, nullptr));
    CHECK(sqlite3_step(stmt));
    sqlite3_finalize(stmt);
    stmt = nullptr;

    config_id = sqlite3_last_insert_rowid(db);

    sql = "INSERT INTO bios_settings (config_id, \"key\", value) "
          "VALUES (?, ?, ?);";
    for (const auto &[key, value] : bios_settings) {
      if (!prepare_stmt(sql, stmt))
        return 0;
      CHECK(sqlite3_bind_int64(stmt, 1, config_id));
      CHECK(sqlite3_bind_text(stmt, 2, key.c_str(), -1, nullptr));
      CHECK(sqlite3_bind_text(stmt, 3, value.c_str(), -1, nullptr));
      CHECK(sqlite3_step(stmt));
    }
    sqlite3_finalize(stmt);
    stmt = nullptr;

    sql = "INSERT INTO mapping_functions (config_id, function) "
          "VALUES (?, ?);";
    for (const uint64_t &function : dram_layout.h_fns) {
      if (!prepare_stmt(sql, stmt))
        return 0;
      std::string function_str = to_hex_string(function);
      CHECK(sqlite3_bind_int64(stmt, 1, config_id));
      CHECK(sqlite3_bind_text(stmt, 2, function_str.c_str(), -1, nullptr));
      CHECK(sqlite3_step(stmt));
    }
    sqlite3_finalize(stmt);

    return config_id;
  };

  if (config_exists()) {
    for (const int64_t c : possible_config_ids) {
      if (mapping_functions_exist(c) && bios_settings_exist(c)) {
        config_id = c;
        return config_id;
      }
    }
  }

  return insert_config();
}

/**
 * @brief    Inserts an experiment into the database.
 *
 * @param    aggressor_rows      The aggressor row count.
 * @param    hammer_count        The used hammer count.
 * @param    target_temp         The set target temperature.
 * @param    comment             The experiment comment.
 * @return   int64_t             The experiment ID on success.
 */
int64_t DB::start_experiment(uint64_t aggressor_rows, uint64_t hammer_count,
                             int64_t target_temp, const std::string &comment) {
  if (config_id == 0) {
    log_error("SQLite | Call load_or_insert_config() first");
    return 0;
  }

  const std::string sql =
      "INSERT INTO experiments (config_id, kernel_version, "
      "distribution_version, app_version, memory_allocator, iter_algorithm, "
      "hammer_pattern, hammer_algorithm, aggressor_rows, hammer_count, "
      "\"end\", comment, target_temp, nop_count) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

  if (!prepare_stmt(sql, stmt_start_experiment))
    return 0;

  const std::string kernel_ver = get_kernel_version();
  const std::string os_release = read_os_release();

  CHECK(sqlite3_bind_int64(stmt_start_experiment, 1, config_id));
  CHECK(sqlite3_bind_text(stmt_start_experiment, 2, kernel_ver.c_str(), -1,
                          nullptr));
  CHECK(sqlite3_bind_text(stmt_start_experiment, 3,
                          os_release.empty() ? nullptr : os_release.c_str(), -1,
                          nullptr));
  CHECK(sqlite3_bind_text(stmt_start_experiment, 4, GIT_NAME, -1, nullptr));
  CHECK(sqlite3_bind_text(stmt_start_experiment, 5,
                          config.memory_allocator.c_str(), -1, nullptr));
  CHECK(sqlite3_bind_text(stmt_start_experiment, 6,
                          config.iter_algorithm.c_str(), -1, nullptr));
  CHECK(sqlite3_bind_text(stmt_start_experiment, 7,
                          config.hammer_pattern.get_description().c_str(), -1,
                          nullptr));
  CHECK(sqlite3_bind_text(stmt_start_experiment, 8,
                          config.hammer_algorithm.c_str(), -1, nullptr));
  CHECK(sqlite3_bind_int64(stmt_start_experiment, 9, aggressor_rows));
  CHECK(sqlite3_bind_int64(stmt_start_experiment, 10, hammer_count));
  CHECK(sqlite3_bind_null(stmt_start_experiment, 11));
  CHECK(sqlite3_bind_text(stmt_start_experiment, 12,
                          comment.empty() ? nullptr : comment.c_str(), -1,
                          nullptr));

  if (!config.target_temps.empty()) {
    CHECK(sqlite3_bind_int64(stmt_start_experiment, 13, target_temp));
  } else {
    CHECK(sqlite3_bind_null(stmt_start_experiment, 13));
  }

  if (config.hammer_algorithm == "machinecode") {
    CHECK(sqlite3_bind_int64(stmt_start_experiment, 14, config.nop_count));
  } else {
    CHECK(sqlite3_bind_null(stmt_start_experiment, 14));
  }

  CHECK(sqlite3_step(stmt_start_experiment));

  experiment_id = sqlite3_last_insert_rowid(db);

  return experiment_id;
}

bool DB::end_experiment() {
  if (experiment_id == 0) {
    log_error("SQLite | Call start_experiment() first");
    return false;
  }

  const std::string sql = "UPDATE experiments "
                          "SET \"end\" = STRFTIME('%Y-%m-%d %H:%M:%f', 'now') "
                          "WHERE id = ?;";

  if (!prepare_stmt(sql, stmt_end_experiment))
    return false;

  CHECK(sqlite3_bind_int64(stmt_end_experiment, 1, experiment_id));
  CHECK(sqlite3_step(stmt_end_experiment));

  return true;
}

/**
 * @brief    Inserts a test into the database.
 *
 * @param    aggressors          The aggressors.
 * @param    hammer_time         The time it took to hammer.
 * @param    victim_init         The initialization of the Victim.
 * @param    aggressor_init      The initialization of the Aggressor.
 * @param    actual_temp         The measured temperature.
 * @return   int64_t             The test ID on success.
 */
int64_t DB::insert_test(const std::vector<uint64_t> &aggressors,
                        uint64_t hammer_time, const uint64_t victim_init,
                        const uint64_t aggressor_init, int64_t actual_temp) {
  const auto insert_aggressor = [this](int64_t test_id, uint64_t aggressor) {
    const std::string sql = "INSERT INTO aggressors (test_id, aggressor, "
                            "  aggressor_bank, aggressor_row, aggressor_col) "
                            "VALUES (?, ?, ?, ?, ?);";
    if (!prepare_stmt(sql, stmt_insert_aggressor))
      return 0;

    std::string aggressor_str = to_hex_string(aggressor);
    DRAMAddr aggressor_dram(aggressor);

    CHECK(sqlite3_bind_int64(stmt_insert_aggressor, 1, test_id));
    CHECK(sqlite3_bind_text(stmt_insert_aggressor, 2, aggressor_str.c_str(), -1,
                            nullptr));
    CHECK(sqlite3_bind_int64(stmt_insert_aggressor, 3, aggressor_dram.bank));
    CHECK(sqlite3_bind_int64(stmt_insert_aggressor, 4, aggressor_dram.row));
    CHECK(sqlite3_bind_int64(stmt_insert_aggressor, 5, aggressor_dram.col));
    CHECK(sqlite3_step(stmt_insert_aggressor));
    return 1;
  };

  if (experiment_id == 0) {
    log_error("SQLite | Call start_experiment() first");
    return 0;
  }

  const std::string sql = "INSERT INTO tests (experiment_id, hammer_time, "
                          "aggressor_init, victim_init, actual_temp) "
                          "VALUES (?, ?, ?, ?, ?);";

  if (!prepare_stmt(sql, stmt_insert_test))
    return 0;

  std::string aggressor_init_str = to_hex_string(aggressor_init, 16);
  std::string victim_init_str = to_hex_string(victim_init, 16);

  CHECK(sqlite3_bind_int64(stmt_insert_test, 1, experiment_id));
  CHECK(sqlite3_bind_int64(stmt_insert_test, 2, hammer_time));
  CHECK(sqlite3_bind_text(stmt_insert_test, 3, aggressor_init_str.c_str(), -1,
                          nullptr));
  CHECK(sqlite3_bind_text(stmt_insert_test, 4, victim_init_str.c_str(), -1,
                          nullptr));
  if (!config.target_temps.empty()) {
    CHECK(sqlite3_bind_int64(stmt_insert_test, 5, actual_temp));
  } else {
    CHECK(sqlite3_bind_null(stmt_insert_test, 5));
  }
  CHECK(sqlite3_step(stmt_insert_test));

  test_id = sqlite3_last_insert_rowid(db);
  for (const uint64_t aggressor : aggressors) {
    if (!insert_aggressor(test_id, aggressor))
      return 0;
  }

  return test_id;
}

/**
 * @brief    Inserts a bit flip into the database.
 *
 * @param    victim              The victim address.
 * @param    bit                 The flipped bit.
 * @param    flipped_to          The bit it flipped to, either 0 or 1.
 * @return   int64_t             The last insert row ID on success.
 */
int64_t DB::insert_bitflip(uint64_t victim, uint8_t bit, uint8_t flipped_to) {
  if (test_id == 0) {
    log_error("SQLite | Call insert_test() first");
    return false;
  }

  const std::string sql =
      "INSERT INTO bitflips (test_id, victim, "
      "  victim_bank, victim_row, victim_col, bit, flipped_to) "
      "VALUES (?, ?, ?, ?, ?, ?, ?);";

  if (!prepare_stmt(sql, stmt_insert_bitflip))
    return 0;

  std::string victim_str = to_hex_string(victim);
  DRAMAddr victim_dram(victim);

  CHECK(sqlite3_bind_int64(stmt_insert_bitflip, 1, test_id));
  CHECK(sqlite3_bind_text(stmt_insert_bitflip, 2, victim_str.c_str(), -1,
                          nullptr));
  CHECK(sqlite3_bind_int64(stmt_insert_bitflip, 3, victim_dram.bank));
  CHECK(sqlite3_bind_int64(stmt_insert_bitflip, 4, victim_dram.row));
  CHECK(sqlite3_bind_int64(stmt_insert_bitflip, 5, victim_dram.col));
  CHECK(sqlite3_bind_int(stmt_insert_bitflip, 6, bit));
  CHECK(sqlite3_bind_int(stmt_insert_bitflip, 7, flipped_to));
  CHECK(sqlite3_step(stmt_insert_bitflip));

  return sqlite3_last_insert_rowid(db);
}
