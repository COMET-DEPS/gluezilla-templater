R"***(
CREATE TABLE configs (
  id INTEGER PRIMARY KEY,
  hostname TEXT NOT NULL,
  dimm0 TEXT,
  dimm1 TEXT,
  dimm2 TEXT,
  dimm3 TEXT,
  row_mask TEXT NOT NULL,
  col_mask TEXT NOT NULL
);
CREATE TABLE bios_settings (
  config_id INTEGER NOT NULL,
  "key" TEXT NOT NULL,
  value TEXT NOT NULL,
  PRIMARY KEY (config_id, "key"),
  FOREIGN KEY (config_id) REFERENCES configs(id) ON DELETE CASCADE
);
CREATE TABLE mapping_functions (
  config_id INTEGER,
  function TEXT NOT NULL,
  PRIMARY KEY (config_id, function),
  FOREIGN KEY (config_id) REFERENCES configs(id) ON DELETE CASCADE
);
CREATE TABLE experiments (
  id INTEGER PRIMARY KEY,
  config_id INTEGER NOT NULL,
  kernel_version TEXT NOT NULL,
  distribution_version TEXT,
  app_version TEXT NOT NULL,
  memory_allocator TEXT NOT NULL,
  iter_algorithm TEXT NOT NULL,
  hammer_pattern TEXT NOT NULL,
  hammer_algorithm TEXT NOT NULL,
  aggressor_rows INTEGER NOT NULL,
  hammer_count INTEGER NOT NULL,
  target_temp INTEGER,
  nop_count INTEGER,
  start TIMESTAMP DEFAULT (STRFTIME('%Y-%m-%d %H:%M:%f', 'now')) NOT NULL,
  "end" TIMESTAMP,
  comment TEXT,
  FOREIGN KEY (config_id) REFERENCES configs(id) ON DELETE CASCADE
);
CREATE TABLE tests (
  id INTEGER PRIMARY KEY,
  experiment_id INTEGER NOT NULL,
  hammer_time INTEGER,
  actual_temp INTEGER,
  aggressor_init TEXT NOT NULL,
  victim_init TEXT NOT NULL,
  timestamp TIMESTAMP DEFAULT (STRFTIME('%Y-%m-%d %H:%M:%f', 'now')) NOT NULL,
  FOREIGN KEY (experiment_id) REFERENCES experiments(id) ON DELETE CASCADE
);
CREATE TABLE bitflips (
  id INTEGER PRIMARY KEY,
  test_id INTEGER NOT NULL,
  victim TEXT NOT NULL,
  victim_bank INTEGER NOT NULL,
  victim_row INTEGER NOT NULL,
  victim_col INTEGER NOT NULL,
  bit INTEGER CHECK(bit >= 0 OR bit <= 7) NOT NULL,
  flipped_to INTEGER CHECK(flipped_to = 0 OR flipped_to = 1) NOT NULL,
  FOREIGN KEY (test_id) REFERENCES tests(id) ON DELETE CASCADE
);
CREATE TABLE aggressors (
  id INTEGER PRIMARY KEY,
  test_id INTEGER NOT NULL,
  aggressor TEXT NOT NULL,
  aggressor_bank INTEGER NOT NULL,
  aggressor_row INTEGER NOT NULL,
  aggressor_col INTEGER NOT NULL,
  FOREIGN KEY (test_id) REFERENCES tests(id) ON DELETE CASCADE
);
)***"
