-- Schema is embedded into C++ at build time and executes on every CLI invocation.
-- IF NOT EXISTS + OR IGNORE preserve persisted state on repeated opens.
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS ecu(
  name TEXT PRIMARY KEY NOT NULL CHECK(length(name) BETWEEN 1 AND 32),
  enabled INTEGER NOT NULL DEFAULT 0 CHECK(enabled IN (0,1))
);
CREATE TABLE IF NOT EXISTS ecu_change(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ecu_name TEXT NOT NULL REFERENCES ecu(name) ON DELETE RESTRICT,
  old_enabled INTEGER NOT NULL CHECK(old_enabled IN (0,1)),
  new_enabled INTEGER NOT NULL CHECK(new_enabled IN (0,1)),
  changed_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),
  CHECK(old_enabled <> new_enabled)
);
CREATE INDEX IF NOT EXISTS idx_ecu_change_ecu_id ON ecu_change(ecu_name,id);
INSERT OR IGNORE INTO ecu(name,enabled) VALUES
  ('ADAS',1),('BCM',1),('CGW',1),('VCU',0);
