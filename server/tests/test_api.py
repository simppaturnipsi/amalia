from uuid import uuid4


def entry_payload(text="Ensimmäinen merkintä", key=None, workstation="ws-01"):
    return {
        "idempotency_key": str(key or uuid4()),
        "client_timestamp": "2026-08-20T12:00:00+03:00",
        "workstation_id": workstation,
        "text": text,
    }


def test_create_append_and_read_in_sequence(client, auth_headers, task):
    first = client.post(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=entry_payload("Yksi"))
    second = client.post(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=entry_payload("Kaksi"))
    assert first.status_code == second.status_code == 201
    rows = client.get(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers).json()
    assert [row["sequence_number"] for row in rows] == [1, 2]
    assert [row["text"] for row in rows] == ["Yksi", "Kaksi"]
    assert rows[0]["server_timestamp"]


def test_idempotency_key_is_stored_only_once(client, auth_headers, task):
    key = uuid4()
    payload = entry_payload(key=key)
    first = client.post(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=payload)
    duplicate = client.post(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=payload)
    assert first.status_code == 201
    assert duplicate.status_code == 200
    assert first.json()["id"] == duplicate.json()["id"]
    assert len(client.get(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers).json()) == 1


def test_log_entry_mutation_routes_do_not_exist(client, auth_headers, task):
    row = client.post(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=entry_payload()).json()
    url = f"/api/v1/tasks/{task['id']}/entries/{row['id']}"
    assert client.put(url, headers=auth_headers, json={"text": "muutos"}).status_code in {404, 405}
    assert client.patch(url, headers=auth_headers, json={"text": "muutos"}).status_code in {404, 405}
    assert client.delete(url, headers=auth_headers).status_code in {404, 405}


def test_delete_requires_confirmation_and_successful_export(client, auth_headers, task):
    url = f"/api/v1/tasks/{task['id']}"
    assert client.delete(url, headers=auth_headers).status_code == 409
    assert client.delete(url + "?confirm=true", headers=auth_headers).status_code == 409
    client.post(f"{url}/entries", headers=auth_headers, json=entry_payload())
    csv_result = client.post(f"{url}/export/csv", headers=auth_headers)
    assert csv_result.status_code == 200
    assert csv_result.content.startswith(b"\xef\xbb\xbf")
    assert client.delete(url + "?confirm=true", headers=auth_headers).status_code == 204
    assert client.get(url, headers=auth_headers).status_code == 404


def test_pdf_export(client, auth_headers, task):
    client.post(f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=entry_payload("ÄÖÅ"))
    response = client.post(f"/api/v1/tasks/{task['id']}/export/pdf", headers=auth_headers)
    assert response.status_code == 200
    assert response.headers["content-type"] == "application/pdf"
    assert response.content.startswith(b"%PDF")
    assert len(response.headers["x-content-sha256"]) == 64


def test_websocket_receives_committed_entry(client, auth_headers, task):
    with client.websocket_connect(f"/ws/tasks/{task['id']}", headers=auth_headers) as socket:
        assert socket.receive_json()["type"] == "connected"
        response = client.post(
            f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=entry_payload("Reaaliaikainen")
        )
        assert response.status_code == 201
        event = socket.receive_json()
        assert event["type"] == "log_entry.created"
        assert event["entry"]["text"] == "Reaaliaikainen"


def test_websocket_disconnect_reconnect_and_rest_catchup(client, auth_headers, task):
    with client.websocket_connect(f"/ws/tasks/{task['id']}", headers=auth_headers) as socket:
        assert socket.receive_json()["type"] == "connected"
    posted = client.post(
        f"/api/v1/tasks/{task['id']}/entries", headers=auth_headers, json=entry_payload("Katkon aikana")
    )
    assert posted.status_code == 201
    with client.websocket_connect(f"/ws/tasks/{task['id']}", headers=auth_headers) as socket:
        assert socket.receive_json()["type"] == "connected"
        catchup = client.get(
            f"/api/v1/tasks/{task['id']}/entries?after_sequence=0", headers=auth_headers
        ).json()
        assert [row["text"] for row in catchup] == ["Katkon aikana"]


def test_authorization_is_enforced(client, task):
    read_only = {"X-Test-User": "reader", "X-Test-Roles": "journal:read"}
    response = client.post(f"/api/v1/tasks/{task['id']}/entries", headers=read_only, json=entry_payload())
    assert response.status_code == 403
