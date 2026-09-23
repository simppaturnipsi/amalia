import sqlite3
from uuid import uuid4


def test_database_trigger_rejects_update_and_individual_delete(app, client, auth_headers, task):
    response = client.post(
        f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers,
        json={"idempotency_key": str(uuid4()), "workstation_id": "ws", "text": "Pysyvä"},
    )
    entry_id = response.json()["id"]
    with app.state.database.connection() as connection:
        with __import__("pytest").raises(sqlite3.IntegrityError, match="immutable"):
            connection.execute("UPDATE log_entries SET text='muutettu' WHERE id=?", (entry_id,))
        with __import__("pytest").raises(sqlite3.IntegrityError, match="cannot be deleted"):
            connection.execute("DELETE FROM log_entries WHERE id=?", (entry_id,))
