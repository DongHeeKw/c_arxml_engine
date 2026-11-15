-- Single source of truth: embedded into the C++ executable by CMake.
CREATE TABLE IF NOT EXISTS ecu(name TEXT PRIMARY KEY, enabled INTEGER NOT NULL CHECK(enabled IN (0,1)));
INSERT OR IGNORE INTO ecu VALUES ('ADAS',1),('BCM',1),('CGW',1),('VCU',0);
