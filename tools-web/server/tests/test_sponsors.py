from __future__ import annotations

from pathlib import Path

from fastapi.testclient import TestClient

from app.config import Settings
from app.main import create_app


def make_client(tmp_path: Path) -> TestClient:
    settings = Settings(
        base_dir=tmp_path,
        host="127.0.0.1",
        port=3413,
        db_path=tmp_path / "sponsors.sqlite3",
        sponsor_admin_token="test-admin-token",
        allowed_origins=("http://127.0.0.1:4173",),
        trust_proxy_headers=False,
    )
    return TestClient(create_app(settings))


def submit(client: TestClient, name: str = "测试用户", amount: str = "20.00"):
    return client.post(
        "/api/cinevault-support/v1/claims",
        json={"displayName": name, "amount": amount, "paymentChannel": "wechat", "website": ""},
    )


def test_claim_requires_admin_confirmation_before_publication(tmp_path: Path) -> None:
    client = make_client(tmp_path)
    created = submit(client)
    assert created.status_code == 201
    claim = created.json()["claim"]
    assert claim["status"] == "pending"
    assert client.get("/api/cinevault-support/v1/sponsors").json()["sponsors"] == []

    unauthorized = client.get("/api/cinevault-support/v1/admin/claims", headers={"X-Admin-Token": "wrong"})
    assert unauthorized.status_code == 401

    confirmed = client.post(
        f"/api/cinevault-support/v1/admin/claims/{claim['id']}",
        headers={"X-Admin-Token": "test-admin-token"},
        json={"action": "confirm"},
    )
    assert confirmed.status_code == 200
    public = client.get("/api/cinevault-support/v1/sponsors").json()["sponsors"]
    assert len(public) == 1
    assert public[0]["displayName"] == "测试用户"
    assert "status" not in public[0]


def test_honeypot_and_input_validation(tmp_path: Path) -> None:
    client = make_client(tmp_path)
    trapped = client.post(
        "/api/cinevault-support/v1/claims",
        json={"displayName": "机器人", "amount": "1", "paymentChannel": "wechat", "website": "spam.example"},
    )
    assert trapped.status_code == 400
    assert submit(client, name="", amount="20").status_code == 422
    assert submit(client, amount="0").status_code == 400
    assert submit(client, amount="1.001").status_code == 400


def test_rate_limit_allows_three_recent_claims(tmp_path: Path) -> None:
    client = make_client(tmp_path)
    assert submit(client, "用户一").status_code == 201
    assert submit(client, "用户二").status_code == 201
    assert submit(client, "用户三").status_code == 201
    limited = submit(client, "用户四")
    assert limited.status_code == 429


def test_health_and_security_headers(tmp_path: Path) -> None:
    client = make_client(tmp_path)
    response = client.get("/api/cinevault-support/v1/health")
    assert response.status_code == 200
    assert response.json()["service"] == "cinevault-support"
    assert response.headers["x-content-type-options"] == "nosniff"
