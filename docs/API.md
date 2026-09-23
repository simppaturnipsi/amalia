# Amalia API

Base path through the reverse proxy: `/amalia/api/v1/`.

| Method | Path | Permission | Purpose |
|---|---|---|---|
| GET | `/me` | authenticated | Current server-validated identity |
| POST | `/tasks` | `journal:write` | Create a task |
| GET | `/tasks` | `journal:read` | List active tasks |
| GET | `/tasks/{id}` | `journal:read` | Read task metadata |
| POST | `/tasks/{id}/close` | `journal:write` | Close task/set ending time |
| POST | `/tasks/{id}/entries` | `journal:write` | Append immutable row |
| GET | `/tasks/{id}/entries?after_sequence=N` | `journal:read` | Read rows in sequence order |
| POST | `/tasks/{id}/export/csv` | `journal:export` | Server CSV export |
| POST | `/tasks/{id}/export/pdf` | `journal:export` | Server PDF export |
| DELETE | `/tasks/{id}?confirm=true` | `journal:delete` | Delete whole exported journal |

There are intentionally no PUT, PATCH or DELETE endpoints for LogEntry.
Database triggers also reject direct row update/deletion. The client-generated
`idempotency_key` must be a UUID. A repeated UUID returns the previously stored
row and does not allocate a second sequence number.

## WebSocket

`wss://host/amalia/ws/tasks/{task_id}` uses the same authentication and
`journal:read` permission. Qt sends the access token in the Authorization header.

On connect:

```json
{"type":"connected","task_id":42}
```

After a committed row:

```json
{"type":"log_entry.created","task_id":42,"entry":{"sequence_number":7}}
```

The event is sent only after SQLite commit. Reconnecting clients request missing
rows with `after_sequence`; WebSocket delivery alone is not used as durable state.

## Schema and sequencing

Migration `server/migrations/001_initial.sql` defines `tasks`, `log_entries`,
`exports`, `task_deletions` and `schema_migrations`. SQLite uses WAL,
`synchronous=FULL`, foreign keys and a 30-second busy timeout. `BEGIN IMMEDIATE`
serializes sequence allocation. All SQL values use bound parameters.
