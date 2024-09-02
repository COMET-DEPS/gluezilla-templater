R"***(
PRAGMA writable_schema = ON;

UPDATE sqlite_master
SET sql = REPLACE(sql,
    'FOREIGN KEY (config_id) REFERENCES config(id)',
    'FOREIGN KEY (config_id) REFERENCES configs(id) ON DELETE CASCADE'
  )
WHERE name IN ('bios_settings', 'mapping_functions', 'experiments') AND type = 'table';

UPDATE sqlite_master
SET sql = REPLACE(sql,
    'FOREIGN KEY (experiment_id) REFERENCES experiments(id)',
    'FOREIGN KEY (experiment_id) REFERENCES experiments(id) ON DELETE CASCADE'
  )
WHERE name = 'bitflips' AND type = 'table';

UPDATE sqlite_master
SET sql = REPLACE(sql,
    'FOREIGN KEY (bitflip_id) REFERENCES bitflips(id)',
    'FOREIGN KEY (bitflip_id) REFERENCES bitflips(id) ON DELETE CASCADE'
  )
WHERE name = 'aggressors' AND type = 'table';

PRAGMA writable_schema = OFF;
)***"