from __future__ import annotations

import hashlib
import hmac
import re
import sqlite3
import uuid
from datetime import datetime, timedelta, timezone
from decimal import Decimal

from fastapi import HTTPException, status

from .config import Settings


PAYMENT_CHANNELS = {"wechat", "alipay"}
CLAIM_STATUSES = {"pending", "confirmed", "rejected"}
MAX_AMOUNT_CENTS = 9_999_999
MAX_RECENT_SUBMISSIONS = 3
RATE_LIMIT_WINDOW_MINUTES = 10
PUBLIC_ID_PATTERN = re.compile(r"^[0-9a-f]{32}$")


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def normalize_display_name(value: str) -> str:
    normalized = " ".join(str(value).split())
    if not 1 <= len(normalized) <= 24:
        raise HTTPException(status_code=400, detail="称呼长度必须为 1 到 24 个字符。")
    if any(ord(char) < 32 for char in normalized):
        raise HTTPException(status_code=400, detail="称呼包含非法字符。")
    return normalized


def normalize_amount_cents(value: Decimal) -> int:
    if not value.is_finite() or value <= 0:
        raise HTTPException(status_code=400, detail="赞助金额必须大于 0。")
    quantized = value.quantize(Decimal("0.01"))
    if value != quantized:
        raise HTTPException(status_code=400, detail="赞助金额最多保留两位小数。")
    cents = int(quantized * 100)
    if cents > MAX_AMOUNT_CENTS:
        raise HTTPException(status_code=400, detail="赞助金额超出允许范围。")
    return cents


def normalize_payment_channel(value: str) -> str:
    normalized = str(value).strip().lower()
    if normalized not in PAYMENT_CHANNELS:
        raise HTTPException(status_code=400, detail="支付方式只能选择微信或支付宝。")
    return normalized


def normalize_public_id(value: str) -> str:
    normalized = str(value).strip().lower()
    if not PUBLIC_ID_PATTERN.fullmatch(normalized):
        raise HTTPException(status_code=404, detail="赞助记录不存在。")
    return normalized


def build_client_hash(settings: Settings, client_identity: str) -> str:
    salt = settings.sponsor_admin_token or str(settings.db_path)
    return hashlib.sha256(f"{salt}|{client_identity}".encode("utf-8")).hexdigest()


def verify_sponsor_admin(settings: Settings, provided_token: str | None) -> None:
    expected = settings.sponsor_admin_token
    if not expected:
        raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="赞助管理员令牌尚未配置。")
    if not hmac.compare_digest(expected, (provided_token or "").strip()):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="管理员令牌无效。")


def _serialize_claim(row: sqlite3.Row, *, include_status: bool = True) -> dict[str, object]:
    payload: dict[str, object] = {
        "id": str(row["public_id"]),
        "displayName": str(row["display_name"]),
        "amount": f"{int(row['amount_cents']) / 100:.2f}",
        "paymentChannel": str(row["payment_channel"]),
        "submittedAt": str(row["submitted_at"]),
        "confirmedAt": str(row["confirmed_at"] or ""),
    }
    if include_status:
        payload["status"] = str(row["status"])
    return payload


def create_sponsor_claim(conn: sqlite3.Connection, *, settings: Settings, display_name: str, amount: Decimal, payment_channel: str, client_identity: str) -> dict[str, object]:
    normalized_name = normalize_display_name(display_name)
    amount_cents = normalize_amount_cents(amount)
    normalized_channel = normalize_payment_channel(payment_channel)
    client_hash = build_client_hash(settings, client_identity)
    cutoff = (datetime.now(timezone.utc) - timedelta(minutes=RATE_LIMIT_WINDOW_MINUTES)).isoformat()
    recent_count = conn.execute("SELECT COUNT(*) FROM sponsor_claims WHERE client_hash = ? AND submitted_at >= ?", (client_hash, cutoff)).fetchone()[0]
    if int(recent_count) >= MAX_RECENT_SUBMISSIONS:
        raise HTTPException(status_code=429, detail="提交过于频繁，请稍后再试。")
    public_id = uuid.uuid4().hex
    now = utc_now_iso()
    conn.execute(
        "INSERT INTO sponsor_claims (public_id, display_name, amount_cents, payment_channel, status, client_hash, submitted_at, confirmed_at, updated_at) VALUES (?, ?, ?, ?, 'pending', ?, ?, NULL, ?)",
        (public_id, normalized_name, amount_cents, normalized_channel, client_hash, now, now),
    )
    row = conn.execute("SELECT * FROM sponsor_claims WHERE public_id = ?", (public_id,)).fetchone()
    assert row is not None
    return _serialize_claim(row)


def get_sponsor_claim(conn: sqlite3.Connection, public_id: str) -> dict[str, object] | None:
    normalized = normalize_public_id(public_id)
    row = conn.execute("SELECT * FROM sponsor_claims WHERE public_id = ?", (normalized,)).fetchone()
    return _serialize_claim(row) if row else None


def list_public_sponsors(conn: sqlite3.Connection) -> list[dict[str, object]]:
    rows = conn.execute("SELECT * FROM sponsor_claims WHERE status = 'confirmed' ORDER BY submitted_at DESC, id DESC").fetchall()
    return [_serialize_claim(row, include_status=False) for row in rows]


def list_admin_claims(conn: sqlite3.Connection, claim_status: str = "pending") -> list[dict[str, object]]:
    normalized = str(claim_status).strip().lower()
    if normalized not in CLAIM_STATUSES:
        raise HTTPException(status_code=400, detail="记录状态无效。")
    rows = conn.execute("SELECT * FROM sponsor_claims WHERE status = ? ORDER BY submitted_at DESC, id DESC", (normalized,)).fetchall()
    return [_serialize_claim(row) for row in rows]


def update_claim_status(conn: sqlite3.Connection, *, public_id: str, action: str) -> dict[str, object]:
    normalized_id = normalize_public_id(public_id)
    normalized_action = str(action).strip().lower()
    if normalized_action not in {"confirm", "reject"}:
        raise HTTPException(status_code=400, detail="操作只能是 confirm 或 reject。")
    row = conn.execute("SELECT * FROM sponsor_claims WHERE public_id = ?", (normalized_id,)).fetchone()
    if row is None:
        raise HTTPException(status_code=404, detail="赞助记录不存在。")
    next_status = "confirmed" if normalized_action == "confirm" else "rejected"
    now = utc_now_iso()
    confirmed_at = now if next_status == "confirmed" else None
    conn.execute("UPDATE sponsor_claims SET status = ?, confirmed_at = ?, updated_at = ? WHERE public_id = ?", (next_status, confirmed_at, now, normalized_id))
    updated = conn.execute("SELECT * FROM sponsor_claims WHERE public_id = ?", (normalized_id,)).fetchone()
    assert updated is not None
    return _serialize_claim(updated)


def sponsor_counts(conn: sqlite3.Connection) -> dict[str, int]:
    rows = conn.execute("SELECT status, COUNT(*) AS total FROM sponsor_claims GROUP BY status").fetchall()
    counts = {"pending": 0, "confirmed": 0, "rejected": 0}
    for row in rows:
        counts[str(row["status"])] = int(row["total"])
    return counts
