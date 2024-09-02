R"***(
DROP VIEW IF EXISTS experiments_bitflips;
CREATE VIEW experiments_bitflips AS
  SELECT e.*, COUNT(DISTINCT t.id) tests, COUNT(b.id) bitflips, COUNT(DISTINCT b.victim || ',' || b.bit || ',' || b.flipped_to) unique_bitflips
  FROM experiments e
    LEFT JOIN tests t ON (e.id = t.experiment_id)
    LEFT JOIN bitflips b ON (t.id = b.test_id)
  GROUP BY e.id
;
DROP VIEW IF EXISTS bitflips_aggressors;
DROP VIEW IF EXISTS bitflips_overview;
CREATE VIEW bitflips_overview AS
  SELECT b.*, t.timestamp, e.aggressor_rows
  FROM bitflips b
    JOIN tests t ON (b.test_id = t.id)
    JOIN experiments e ON (t.experiment_id = e.id)
;
DROP VIEW IF EXISTS addresses;
CREATE VIEW addresses AS
  SELECT b.id, 'RESULT PAIR,' || GROUP_CONCAT(a.aggressor) || ',' || b.victim AS addresses
  FROM bitflips b JOIN aggressors a ON (b.test_id = a.test_id)
  GROUP BY b.id
;
DROP VIEW IF EXISTS configs_overview;
CREATE VIEW configs_overview AS
  SELECT c.id, c.hostname, c.dimm0, c.dimm1, c.dimm2, c.dimm3, bs.bios_settings,
    '{' || m.functions || ', ' || c.row_mask || ', ' || c.col_mask || '}' AS dram_layout,
    eb.experiments, eb.bitflips, eb.unique_bitflips
  FROM configs c
    LEFT JOIN (
      SELECT config_id, '{{' || GROUP_CONCAT(function, ', ') || '}, ' || COUNT(function) || '}' AS functions
      FROM (
        SELECT config_id, function
        FROM mapping_functions
        ORDER BY SUBSTR('0000000000000000' || SUBSTR(function, 3), -16)
      )
      GROUP BY config_id
    ) m ON (c.id = m.config_id)
    LEFT JOIN (
      SELECT config_id, GROUP_CONCAT("key" || ': ' || value, ', ') AS bios_settings
      FROM bios_settings
      GROUP BY config_id
    ) bs ON (c.id = bs.config_id)
    LEFT JOIN (
      SELECT e.config_id, COUNT(DISTINCT e.id) experiments, COUNT(b.id) bitflips, COUNT(DISTINCT b.victim || ',' || b.bit || ',' || b.flipped_to) unique_bitflips
      FROM experiments e
        LEFT JOIN tests t ON (e.id = t.experiment_id)
        JOIN bitflips b ON (t.id = b.test_id)
      GROUP BY e.config_id
    ) eb ON (c.id = eb.config_id)
  GROUP BY c.id
;
)***"
