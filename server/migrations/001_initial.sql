CREATE TABLE tasks (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL CHECK(length(name) BETWEEN 1 AND 200),
    task_number TEXT NOT NULL UNIQUE CHECK(length(task_number) BETWEEN 1 AND 100),
    created_at TEXT NOT NULL,
    created_by TEXT NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('open','closed','deleted')),
    ended_at TEXT,
    exported_at TEXT,
    exported_by TEXT,
    deleted_at TEXT,
    deleted_by TEXT
);

CREATE TABLE log_entries (
    id INTEGER PRIMARY KEY,
    task_id INTEGER NOT NULL,
    sequence_number INTEGER NOT NULL,
    idempotency_key TEXT NOT NULL UNIQUE,
    server_timestamp TEXT NOT NULL,
    client_timestamp TEXT,
    user_id TEXT NOT NULL,
    username TEXT NOT NULL,
    display_name TEXT NOT NULL,
    workstation_id TEXT NOT NULL,
    text TEXT NOT NULL CHECK(length(text) BETWEEN 1 AND 10000),
    created_at TEXT NOT NULL,
    UNIQUE(task_id, sequence_number),
    FOREIGN KEY(task_id) REFERENCES tasks(id) ON DELETE CASCADE
);

CREATE TABLE exports (
    id INTEGER PRIMARY KEY,
    task_id INTEGER NOT NULL,
    format TEXT NOT NULL CHECK(format IN ('pdf','csv','json')),
    created_at TEXT NOT NULL,
    created_by TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    FOREIGN KEY(task_id) REFERENCES tasks(id) ON DELETE CASCADE
);

CREATE TABLE task_deletions (
    id INTEGER PRIMARY KEY,
    task_id INTEGER NOT NULL,
    deleted_at TEXT NOT NULL,
    deleted_by TEXT NOT NULL,
    mode TEXT NOT NULL CHECK(mode IN ('soft','hard')),
    last_exported_at TEXT NOT NULL,
    last_exported_by TEXT NOT NULL
);

CREATE INDEX idx_log_entries_task_sequence ON log_entries(task_id, sequence_number);
CREATE INDEX idx_tasks_status ON tasks(status, deleted_at);

-- Append-only enforcement also protects against accidental future SQL/API changes.
CREATE TRIGGER log_entries_no_update
BEFORE UPDATE ON log_entries
BEGIN
    SELECT RAISE(ABORT, 'log entries are immutable');
END;

CREATE TRIGGER log_entries_no_delete
BEFORE DELETE ON log_entries
WHEN NOT EXISTS (SELECT 1 FROM task_deletions WHERE task_id=OLD.task_id AND mode='hard')
BEGIN
    SELECT RAISE(ABORT, 'individual log entries cannot be deleted');
END;
