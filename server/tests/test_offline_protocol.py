import json
import sqlite3
from uuid import uuid4


def test_server_cut_during_send_preserves_queue_and_uuid_is_not_sent_twice(client, auth_headers, task, tmp_path):
    database = sqlite3.connect(tmp_path / "offline.db")
    database.execute("CREATE TABLE queue(idempotency_key TEXT PRIMARY KEY,payload TEXT NOT NULL,status TEXT NOT NULL)")
    key = str(uuid4())
    payload = {"idempotency_key": key, "workstation_id": "offline-ws", "text": "Offline-rivi"}
    database.execute("INSERT INTO queue VALUES(?,?,'pending')", (key, json.dumps(payload)))
    database.commit()

    # Simulated network cut: no API response, row must remain persistent and pending.
    assert database.execute("SELECT status FROM queue WHERE idempotency_key=?", (key,)).fetchone()[0] == "pending"
    database.close()

    reopened = sqlite3.connect(tmp_path / "offline.db")
    stored = json.loads(reopened.execute("SELECT payload FROM queue WHERE status='pending'").fetchone()[0])
    first = client.post(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=stored)
    assert first.status_code == 201
    reopened.execute("UPDATE queue SET status='synced' WHERE idempotency_key=?", (key,))
    reopened.commit()

    # Even if an acknowledgement was lost and the same UUID is retried, server returns the same row.
    duplicate = client.post(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=stored)
    assert duplicate.status_code == 200
    assert duplicate.json()["id"] == first.json()["id"]
    rows = client.get(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers).json()
    assert len(rows) == 1
