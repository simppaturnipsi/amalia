from concurrent.futures import ThreadPoolExecutor
from uuid import uuid4


def _post(client, task_id, index):
    headers = {
        "X-Test-User": f"user-{index % 2}",
        "X-Test-Display-Name": f"User {index % 2}",
        "X-Test-Roles": "journal:read,journal:write",
    }
    return client.post(
        f"/api/v1/tasks/{task_id}/entries",
        headers=headers,
        json={
            "idempotency_key": str(uuid4()),
            "workstation_id": f"ws-{index % 2}",
            "text": f"Concurrent row {index}",
        },
    )


def test_two_workstations_append_concurrently(client, auth_headers, task):
    with ThreadPoolExecutor(max_workers=2) as pool:
        responses = list(pool.map(lambda index: _post(client, task["id"], index), range(2)))
    assert all(response.status_code == 201 for response in responses)
    rows = client.get(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers).json()
    assert [row["sequence_number"] for row in rows] == [1, 2]
    assert {row["workstation_id"] for row in rows} == {"ws-0", "ws-1"}


def test_ten_concurrent_inserts_have_unique_contiguous_sequences(client, auth_headers, task):
    with ThreadPoolExecutor(max_workers=10) as pool:
        responses = list(pool.map(lambda index: _post(client, task["id"], index), range(10)))
    assert all(response.status_code == 201 for response in responses)
    rows = client.get(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers).json()
    assert [row["sequence_number"] for row in rows] == list(range(1, 11))
