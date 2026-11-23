from __future__ import annotations

import json
import mimetypes
import os
import re
import secrets
import shutil
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any
from urllib.parse import quote
from uuid import uuid4

from fastapi import FastAPI, File, Form, Header, HTTPException, Query, Request, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse
from pydantic import BaseModel, Field


BASE_DIR = Path(__file__).resolve().parents[1]
ADMIN_DIR = BASE_DIR.parent / "admin"
STORAGE_DIR = Path(os.getenv("DIT_FEEDBACK_STORAGE_DIR", str(BASE_DIR / "storage"))).resolve()
UPLOADS_DIR = STORAGE_DIR / "uploads"
DB_PATH = Path(os.getenv("DIT_FEEDBACK_DB_PATH", str(STORAGE_DIR / "feedback.sqlite3"))).resolve()
ADMIN_USERNAME = os.getenv("DIT_FEEDBACK_ADMIN_USERNAME", "admin")
ADMIN_PASSWORD = os.getenv("DIT_FEEDBACK_ADMIN_PASSWORD", "change-this-password")
PUBLIC_BASE_URL = os.getenv("DIT_FEEDBACK_PUBLIC_BASE_URL", "").strip().rstrip("/")
ADMIN_TOKEN_TTL_HOURS = 24
MAX_ATTACHMENT_SIZE_BYTES = 100 * 1024 * 1024


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sanitize_filename(filename: str) -> str:
    cleaned = Path(filename or "file").name
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", cleaned).strip("._")
    return cleaned or "file"


def normalize_attachment_mime_type(content_type: str | None, filename: str) -> str:
    normalized = str(content_type or "").split(";", 1)[0].strip().lower()
    if normalized and normalized != "application/octet-stream":
        return normalized
    guessed, _ = mimetypes.guess_type(filename or "")
    if guessed:
        return guessed.lower()
    return normalized or "application/octet-stream"


def attachment_public_path(conversation_id: str, attachment_id: str, filename: str) -> str:
    return f"/files/{conversation_id}/{attachment_id}/{sanitize_filename(filename)}"


def public_http_base_url(request: Request) -> str:
    if PUBLIC_BASE_URL:
        return PUBLIC_BASE_URL
    return str(request.base_url).rstrip("/")


def public_ws_base_url(request: Request) -> str:
    base_url = public_http_base_url(request)
    if base_url.startswith("https://"):
        return "wss://" + base_url[len("https://") :]
    if base_url.startswith("http://"):
        return "ws://" + base_url[len("http://") :]
    return base_url


def json_loads(value: str | None, fallback: Any) -> Any:
    if not value:
        return fallback
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        return fallback


def latest_preview_from_message(text: str, attachments: list[dict[str, Any]]) -> str:
    normalized_text = text.strip()
    if normalized_text:
        return normalized_text[:200]
    if attachments:
        return str(attachments[0].get("name") or "新消息")[:200]
    return ""


class ClientSessionPayload(BaseModel):
    client_id: str | None = None
    client_token: str | None = None
    nickname: str = Field(min_length=1, max_length=64)
    contact: str = Field(min_length=1, max_length=128)
    app_version: str = ""
    system_summary: str = ""
    project_name: str = ""
    project_path: str = ""


class AdminLoginPayload(BaseModel):
    username: str
    password: str


class AdminStatusPayload(BaseModel):
    status: str = Field(pattern="^(pending|in_progress|resolved)$")


@dataclass
class AdminToken:
    token: str
    expires_at: datetime


class Database:
    def __init__(self, db_path: Path) -> None:
        self.db_path = db_path
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        UPLOADS_DIR.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.db_path, check_same_thread=False)
        connection.row_factory = sqlite3.Row
        return connection

    def _init_db(self) -> None:
        with self._connect() as conn:
            conn.executescript(
                """
                PRAGMA journal_mode=WAL;
                PRAGMA foreign_keys=ON;

                CREATE TABLE IF NOT EXISTS conversations (
                    id TEXT PRIMARY KEY,
                    client_id TEXT NOT NULL UNIQUE,
                    client_token TEXT NOT NULL UNIQUE,
                    nickname TEXT NOT NULL,
                    contact TEXT NOT NULL,
                    status TEXT NOT NULL DEFAULT 'pending',
                    app_version TEXT NOT NULL DEFAULT '',
                    system_summary TEXT NOT NULL DEFAULT '',
                    project_name TEXT NOT NULL DEFAULT '',
                    project_path TEXT NOT NULL DEFAULT '',
                    latest_preview TEXT NOT NULL DEFAULT '',
                    latest_message_at TEXT NOT NULL DEFAULT '',
                    created_at TEXT NOT NULL,
                    updated_at TEXT NOT NULL,
                    client_last_seen_message_id INTEGER NOT NULL DEFAULT 0,
                    admin_last_seen_message_id INTEGER NOT NULL DEFAULT 0
                );

                CREATE TABLE IF NOT EXISTS messages (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    conversation_id TEXT NOT NULL,
                    sender_role TEXT NOT NULL,
                    text TEXT NOT NULL DEFAULT '',
                    attachments_json TEXT NOT NULL DEFAULT '[]',
                    created_at TEXT NOT NULL,
                    FOREIGN KEY(conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
                );

                CREATE INDEX IF NOT EXISTS idx_messages_conversation ON messages(conversation_id, id);
                CREATE INDEX IF NOT EXISTS idx_conversations_latest_message ON conversations(latest_message_at DESC, updated_at DESC);
                """
            )

    def _conversation_summary(self, request: Request, row: sqlite3.Row) -> dict[str, Any]:
        return {
            "conversation_id": row["id"],
            "client_id": row["client_id"],
            "nickname": row["nickname"],
            "contact": row["contact"],
            "status": row["status"],
            "app_version": row["app_version"],
            "system_summary": row["system_summary"],
            "project_name": row["project_name"],
            "project_path": row["project_path"],
            "latest_preview": row["latest_preview"],
            "latest_message_at": row["latest_message_at"],
            "created_at": row["created_at"],
            "updated_at": row["updated_at"],
            "unread_admin": row["unread_admin"] if "unread_admin" in row.keys() else 0,
            "unread_client": row["unread_client"] if "unread_client" in row.keys() else 0,
        }

    def _attach_client_realtime(self, request: Request, summary: dict[str, Any], row: sqlite3.Row) -> dict[str, Any]:
        enriched = dict(summary)
        enriched["client_token"] = row["client_token"]
        ws_base_url = public_ws_base_url(request)
        ws_path = f"/ws/client/{row['client_id']}?token={quote(row['client_token'])}"
        enriched["client_ws_url"] = f"{ws_base_url}{ws_path}"
        return enriched

    def _message_payload(self, request: Request, row: sqlite3.Row) -> dict[str, Any]:
        attachments = json_loads(row["attachments_json"], [])
        normalized_attachments = []
        base_url = public_http_base_url(request)
        for attachment in attachments:
            attachment = dict(attachment)
            relative_url = attachment.get("url", "")
            attachment["mime_type"] = normalize_attachment_mime_type(
                attachment.get("mime_type"),
                str(attachment.get("name") or relative_url),
            )
            attachment["url"] = relative_url if relative_url.startswith("http") else base_url + relative_url
            normalized_attachments.append(attachment)
        return {
            "id": row["id"],
            "conversation_id": row["conversation_id"],
            "sender_role": row["sender_role"],
            "text": row["text"],
            "attachments": normalized_attachments,
            "created_at": row["created_at"],
        }

    def get_or_create_client_conversation(self, request: Request, payload: ClientSessionPayload) -> tuple[dict[str, Any], list[dict[str, Any]], bool]:
        with self._connect() as conn:
            row = None
            metadata_changed = False
            normalized_nickname = payload.nickname.strip()
            normalized_contact = payload.contact.strip()
            normalized_app_version = payload.app_version.strip()
            normalized_system_summary = payload.system_summary.strip()
            normalized_project_name = payload.project_name.strip()
            normalized_project_path = payload.project_path.strip()
            if payload.client_id and payload.client_token:
                row = conn.execute(
                    "SELECT * FROM conversations WHERE client_id = ? AND client_token = ? LIMIT 1",
                    (payload.client_id.strip(), payload.client_token.strip()),
                ).fetchone()

            if row is None:
                conversation_id = f"conv_{uuid4().hex}"
                client_id = f"client_{uuid4().hex}"
                client_token = secrets.token_urlsafe(32)
                now = utc_now()
                conn.execute(
                    """
                    INSERT INTO conversations (
                        id, client_id, client_token, nickname, contact, status,
                        app_version, system_summary, project_name, project_path,
                        latest_preview, latest_message_at, created_at, updated_at
                    ) VALUES (?, ?, ?, ?, ?, 'pending', ?, ?, ?, ?, '', '', ?, ?)
                    """,
                    (
                        conversation_id,
                        client_id,
                        client_token,
                        normalized_nickname,
                        normalized_contact,
                        normalized_app_version,
                        normalized_system_summary,
                        normalized_project_name,
                        normalized_project_path,
                        now,
                        now,
                    ),
                )
                conn.commit()
                row = conn.execute("SELECT * FROM conversations WHERE id = ?", (conversation_id,)).fetchone()
            else:
                metadata_changed = any(
                    (
                        row["nickname"] != normalized_nickname,
                        row["contact"] != normalized_contact,
                        row["app_version"] != normalized_app_version,
                        row["system_summary"] != normalized_system_summary,
                        row["project_name"] != normalized_project_name,
                        row["project_path"] != normalized_project_path,
                    )
                )
                if metadata_changed:
                    conn.execute(
                        """
                        UPDATE conversations
                        SET nickname = ?, contact = ?, app_version = ?, system_summary = ?,
                            project_name = ?, project_path = ?, updated_at = ?
                        WHERE id = ?
                        """,
                        (
                            normalized_nickname,
                            normalized_contact,
                            normalized_app_version,
                            normalized_system_summary,
                            normalized_project_name,
                            normalized_project_path,
                            utc_now(),
                            row["id"],
                        ),
                    )
                    conn.commit()
                    row = conn.execute("SELECT * FROM conversations WHERE id = ?", (row["id"],)).fetchone()

            messages = self._list_messages(conn, request, row["id"])
            self._mark_seen(conn, row["id"], "client")
            summary_row = self._conversation_row_with_unread(conn, row["id"])
            summary = self._conversation_summary(request, summary_row)
            return self._attach_client_realtime(request, summary, row), messages, metadata_changed

    def authenticate_client(self, conversation_id: str, client_id: str, client_token: str) -> sqlite3.Row:
        with self._connect() as conn:
            row = conn.execute(
                """
                SELECT * FROM conversations
                WHERE id = ? AND client_id = ? AND client_token = ?
                LIMIT 1
                """,
                (conversation_id, client_id, client_token),
            ).fetchone()
            if row is None:
                raise HTTPException(status_code=401, detail="客户端身份验证失败。")
            return row

    def list_client_messages(self, request: Request, conversation_id: str, client_id: str, client_token: str) -> tuple[dict[str, Any], list[dict[str, Any]]]:
        with self._connect() as conn:
            row = conn.execute(
                "SELECT * FROM conversations WHERE id = ? AND client_id = ? AND client_token = ? LIMIT 1",
                (conversation_id, client_id, client_token),
            ).fetchone()
            if row is None:
                raise HTTPException(status_code=401, detail="客户端身份验证失败。")
            messages = self._list_messages(conn, request, conversation_id)
            self._mark_seen(conn, conversation_id, "client")
            summary_row = self._conversation_row_with_unread(conn, conversation_id)
            summary = self._conversation_summary(request, summary_row)
            return self._attach_client_realtime(request, summary, row), messages

    def append_message(
        self,
        request: Request,
        conversation_id: str,
        sender_role: str,
        text: str,
        attachments: list[dict[str, Any]],
        app_version: str = "",
        system_summary: str = "",
        project_name: str = "",
        project_path: str = "",
    ) -> tuple[dict[str, Any], dict[str, Any]]:
        normalized_text = text.strip()
        if not normalized_text and not attachments:
            raise HTTPException(status_code=400, detail="消息内容和附件不能同时为空。")

        with self._connect() as conn:
            conversation = conn.execute("SELECT * FROM conversations WHERE id = ? LIMIT 1", (conversation_id,)).fetchone()
            if conversation is None:
                raise HTTPException(status_code=404, detail="会话不存在。")

            now = utc_now()
            latest_preview = latest_preview_from_message(normalized_text, attachments)
            conn.execute(
                """
                INSERT INTO messages (conversation_id, sender_role, text, attachments_json, created_at)
                VALUES (?, ?, ?, ?, ?)
                """,
                (conversation_id, sender_role, normalized_text, json.dumps(attachments, ensure_ascii=False), now),
            )
            message_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0]
            last_seen_column = "client_last_seen_message_id" if sender_role == "client" else "admin_last_seen_message_id"
            conn.execute(
                f"""
                UPDATE conversations
                SET latest_preview = ?, latest_message_at = ?, updated_at = ?,
                    app_version = ?, system_summary = ?, project_name = ?, project_path = ?,
                    {last_seen_column} = ?
                WHERE id = ?
                """,
                (
                    latest_preview[:200],
                    now,
                    now,
                    app_version.strip() or conversation["app_version"],
                    system_summary.strip() or conversation["system_summary"],
                    project_name.strip() or conversation["project_name"],
                    project_path.strip() or conversation["project_path"],
                    message_id,
                    conversation_id,
                ),
            )
            conn.commit()

            message_row = conn.execute("SELECT * FROM messages WHERE id = ?", (message_id,)).fetchone()
            summary_row = self._conversation_row_with_unread(conn, conversation_id)
            return self._conversation_summary(request, summary_row), self._message_payload(request, message_row)

    def delete_client_message(
        self,
        request: Request,
        conversation_id: str,
        message_id: int,
        client_id: str,
        client_token: str,
    ) -> tuple[dict[str, Any], int]:
        with self._connect() as conn:
            conversation_row = conn.execute(
                "SELECT * FROM conversations WHERE id = ? AND client_id = ? AND client_token = ? LIMIT 1",
                (conversation_id, client_id, client_token),
            ).fetchone()
            if conversation_row is None:
                raise HTTPException(status_code=401, detail="客户端身份验证失败。")

            message_row = conn.execute(
                "SELECT * FROM messages WHERE id = ? AND conversation_id = ? LIMIT 1",
                (message_id, conversation_id),
            ).fetchone()
            if message_row is None:
                raise HTTPException(status_code=404, detail="消息不存在。")
            if message_row["sender_role"] != "client":
                raise HTTPException(status_code=403, detail="只能删除自己发送的消息。")

            self._delete_messages(conn, conversation_id, [message_row])
            summary_row = self._conversation_row_with_unread(conn, conversation_id)
            summary = self._conversation_summary(request, summary_row)
            return self._attach_client_realtime(request, summary, conversation_row), int(message_id)

    def clear_client_messages(
        self,
        request: Request,
        conversation_id: str,
        client_id: str,
        client_token: str,
    ) -> tuple[dict[str, Any], list[int]]:
        with self._connect() as conn:
            conversation_row = conn.execute(
                "SELECT * FROM conversations WHERE id = ? AND client_id = ? AND client_token = ? LIMIT 1",
                (conversation_id, client_id, client_token),
            ).fetchone()
            if conversation_row is None:
                raise HTTPException(status_code=401, detail="客户端身份验证失败。")

            message_rows = conn.execute(
                """
                SELECT * FROM messages
                WHERE conversation_id = ? AND sender_role = 'client'
                ORDER BY id ASC
                """,
                (conversation_id,),
            ).fetchall()

            deleted_ids = [int(row["id"]) for row in message_rows]
            if deleted_ids:
                self._delete_messages(conn, conversation_id, message_rows)
            else:
                summary_row = self._conversation_row_with_unread(conn, conversation_id)
                summary = self._conversation_summary(request, summary_row)
                return self._attach_client_realtime(request, summary, conversation_row), []

            summary_row = self._conversation_row_with_unread(conn, conversation_id)
            summary = self._conversation_summary(request, summary_row)
            return self._attach_client_realtime(request, summary, conversation_row), deleted_ids

    def list_conversations(self, request: Request) -> list[dict[str, Any]]:
        with self._connect() as conn:
            rows = conn.execute(
                """
                SELECT c.*,
                       (
                           SELECT COUNT(*)
                           FROM messages m
                           WHERE m.conversation_id = c.id
                             AND m.sender_role = 'client'
                             AND m.id > c.admin_last_seen_message_id
                       ) AS unread_admin,
                       (
                           SELECT COUNT(*)
                           FROM messages m
                           WHERE m.conversation_id = c.id
                             AND m.sender_role = 'admin'
                             AND m.id > c.client_last_seen_message_id
                       ) AS unread_client
                FROM conversations c
                ORDER BY
                    CASE WHEN c.latest_message_at = '' THEN c.updated_at ELSE c.latest_message_at END DESC,
                    c.updated_at DESC
                """
            ).fetchall()
            return [self._conversation_summary(request, row) for row in rows]

    def list_admin_messages(self, request: Request, conversation_id: str) -> tuple[dict[str, Any], list[dict[str, Any]]]:
        with self._connect() as conn:
            row = conn.execute("SELECT * FROM conversations WHERE id = ? LIMIT 1", (conversation_id,)).fetchone()
            if row is None:
                raise HTTPException(status_code=404, detail="会话不存在。")
            messages = self._list_messages(conn, request, conversation_id)
            self._mark_seen(conn, conversation_id, "admin")
            summary_row = self._conversation_row_with_unread(conn, conversation_id)
            return self._conversation_summary(request, summary_row), messages

    def update_status(self, request: Request, conversation_id: str, status: str) -> dict[str, Any]:
        with self._connect() as conn:
            row = conn.execute("SELECT * FROM conversations WHERE id = ? LIMIT 1", (conversation_id,)).fetchone()
            if row is None:
                raise HTTPException(status_code=404, detail="会话不存在。")
            conn.execute(
                "UPDATE conversations SET status = ?, updated_at = ? WHERE id = ?",
                (status, utc_now(), conversation_id),
            )
            conn.commit()
            summary_row = self._conversation_row_with_unread(conn, conversation_id)
            return self._conversation_summary(request, summary_row)

    def delete_conversation(self, conversation_id: str) -> dict[str, Any]:
        with self._connect() as conn:
            row = conn.execute("SELECT * FROM conversations WHERE id = ? LIMIT 1", (conversation_id,)).fetchone()
            if row is None:
                raise HTTPException(status_code=404, detail="会话不存在。")

            client_id = row["client_id"]
            conn.execute("DELETE FROM conversations WHERE id = ?", (conversation_id,))
            conn.commit()

        shutil.rmtree(UPLOADS_DIR / conversation_id, ignore_errors=True)
        return {
            "conversation_id": conversation_id,
            "client_id": client_id,
        }

    def get_client_token(self, client_id: str) -> str | None:
        with self._connect() as conn:
            row = conn.execute("SELECT client_token FROM conversations WHERE client_id = ? LIMIT 1", (client_id,)).fetchone()
            return row["client_token"] if row else None

    def _conversation_row_with_unread(self, conn: sqlite3.Connection, conversation_id: str) -> sqlite3.Row:
        row = conn.execute(
            """
            SELECT c.*,
                   (
                       SELECT COUNT(*)
                       FROM messages m
                       WHERE m.conversation_id = c.id
                         AND m.sender_role = 'client'
                         AND m.id > c.admin_last_seen_message_id
                   ) AS unread_admin,
                   (
                       SELECT COUNT(*)
                       FROM messages m
                       WHERE m.conversation_id = c.id
                         AND m.sender_role = 'admin'
                         AND m.id > c.client_last_seen_message_id
                   ) AS unread_client
            FROM conversations c
            WHERE c.id = ?
            LIMIT 1
            """,
            (conversation_id,),
        ).fetchone()
        if row is None:
            raise HTTPException(status_code=404, detail="会话不存在。")
        return row

    def _mark_seen(self, conn: sqlite3.Connection, conversation_id: str, role: str) -> None:
        latest_row = conn.execute("SELECT COALESCE(MAX(id), 0) FROM messages WHERE conversation_id = ?", (conversation_id,)).fetchone()
        latest_id = latest_row[0] if latest_row else 0
        column = "client_last_seen_message_id" if role == "client" else "admin_last_seen_message_id"
        conn.execute(f"UPDATE conversations SET {column} = ?, updated_at = ? WHERE id = ?", (latest_id, utc_now(), conversation_id))
        conn.commit()

    def _list_messages(self, conn: sqlite3.Connection, request: Request, conversation_id: str) -> list[dict[str, Any]]:
        rows = conn.execute(
            "SELECT * FROM messages WHERE conversation_id = ? ORDER BY id ASC",
            (conversation_id,),
        ).fetchall()
        return [self._message_payload(request, row) for row in rows]

    def _attachment_file_path(self, conversation_id: str, attachment: dict[str, Any]) -> Path:
        stored_name = str(attachment.get("stored_name") or "").strip()
        if not stored_name:
            attachment_id = str(attachment.get("id") or "").strip()
            safe_name = sanitize_filename(str(attachment.get("name") or "file"))
            stored_name = f"{attachment_id}__{safe_name}" if attachment_id else safe_name
        return UPLOADS_DIR / conversation_id / stored_name

    def _delete_message_attachments(self, conversation_id: str, message_rows: list[sqlite3.Row] | tuple[sqlite3.Row, ...]) -> None:
        for row in message_rows:
            for attachment in json_loads(row["attachments_json"], []):
                target = self._attachment_file_path(conversation_id, dict(attachment))
                if target.exists():
                    target.unlink()
        conversation_dir = UPLOADS_DIR / conversation_id
        if conversation_dir.exists():
            try:
                next(conversation_dir.iterdir())
            except StopIteration:
                conversation_dir.rmdir()

    def _refresh_conversation_after_message_mutation(self, conn: sqlite3.Connection, conversation_id: str) -> None:
        latest_row = conn.execute(
            """
            SELECT * FROM messages
            WHERE conversation_id = ?
            ORDER BY id DESC
            LIMIT 1
            """,
            (conversation_id,),
        ).fetchone()

        latest_preview = ""
        latest_message_at = ""
        if latest_row is not None:
            attachments = json_loads(latest_row["attachments_json"], [])
            latest_preview = latest_preview_from_message(latest_row["text"], attachments)
            latest_message_at = latest_row["created_at"]

        conn.execute(
            """
            UPDATE conversations
            SET latest_preview = ?, latest_message_at = ?, updated_at = ?
            WHERE id = ?
            """,
            (latest_preview, latest_message_at, utc_now(), conversation_id),
        )
        conn.commit()

    def _delete_messages(
        self,
        conn: sqlite3.Connection,
        conversation_id: str,
        message_rows: list[sqlite3.Row] | tuple[sqlite3.Row, ...],
    ) -> None:
        if not message_rows:
            return

        ids = [int(row["id"]) for row in message_rows]
        placeholders = ",".join("?" for _ in ids)
        conn.execute(
            f"DELETE FROM messages WHERE conversation_id = ? AND id IN ({placeholders})",
            [conversation_id, *ids],
        )
        conn.commit()
        self._delete_message_attachments(conversation_id, message_rows)
        self._refresh_conversation_after_message_mutation(conn, conversation_id)


class RealtimeHub:
    def __init__(self) -> None:
        self.admin_connections: set[WebSocket] = set()
        self.client_connections: dict[str, set[WebSocket]] = {}

    async def connect_admin(self, websocket: WebSocket) -> None:
        await websocket.accept()
        self.admin_connections.add(websocket)

    async def connect_client(self, client_id: str, websocket: WebSocket) -> None:
        await websocket.accept()
        self.client_connections.setdefault(client_id, set()).add(websocket)

    def disconnect_admin(self, websocket: WebSocket) -> None:
        self.admin_connections.discard(websocket)

    def disconnect_client(self, client_id: str, websocket: WebSocket) -> None:
        clients = self.client_connections.get(client_id)
        if not clients:
            return
        clients.discard(websocket)
        if not clients:
            self.client_connections.pop(client_id, None)

    async def broadcast_message(self, conversation: dict[str, Any], message: dict[str, Any]) -> None:
        payload = {"type": "message.created", "conversation": conversation, "message": message}
        await self._broadcast(self.admin_connections, payload)
        client_connections = self.client_connections.get(conversation["client_id"], set())
        await self._broadcast(client_connections, payload)

    async def broadcast_conversation(self, conversation: dict[str, Any]) -> None:
        payload = {"type": "conversation.updated", "conversation": conversation}
        await self._broadcast(self.admin_connections, payload)
        client_connections = self.client_connections.get(conversation["client_id"], set())
        await self._broadcast(client_connections, payload)

    async def broadcast_message_deleted(self, conversation: dict[str, Any], message_id: int) -> None:
        payload = {"type": "message.deleted", "conversation": conversation, "message_id": message_id}
        await self._broadcast(self.admin_connections, payload)
        client_connections = self.client_connections.get(conversation["client_id"], set())
        await self._broadcast(client_connections, payload)

    async def broadcast_messages_cleared(self, conversation: dict[str, Any], message_ids: list[int]) -> None:
        payload = {
            "type": "messages.cleared",
            "conversation": conversation,
            "message_ids": message_ids,
            "deleted_count": len(message_ids),
        }
        await self._broadcast(self.admin_connections, payload)
        client_connections = self.client_connections.get(conversation["client_id"], set())
        await self._broadcast(client_connections, payload)

    async def broadcast_conversation_deleted(self, conversation_id: str, client_id: str) -> None:
        payload = {
            "type": "conversation.deleted",
            "conversation_id": conversation_id,
            "client_id": client_id,
        }
        await self._broadcast(self.admin_connections, payload)
        client_connections = self.client_connections.get(client_id, set())
        await self._broadcast(client_connections, payload)

    async def _broadcast(self, sockets: set[WebSocket], payload: dict[str, Any]) -> None:
        stale: list[WebSocket] = []
        for socket in list(sockets):
            try:
                await socket.send_json(payload)
            except Exception:
                stale.append(socket)
        for socket in stale:
            sockets.discard(socket)


def parse_bearer_token(authorization: str | None) -> str:
    if not authorization:
        raise HTTPException(status_code=401, detail="缺少管理员身份令牌。")
    prefix = "Bearer "
    if not authorization.startswith(prefix):
        raise HTTPException(status_code=401, detail="管理员身份令牌格式不正确。")
    return authorization[len(prefix) :].strip()


database = Database(DB_PATH)
hub = RealtimeHub()
admin_tokens: dict[str, AdminToken] = {}


def verify_admin_token(token: str) -> None:
    record = admin_tokens.get(token)
    if record is None or record.expires_at < datetime.now(timezone.utc):
        admin_tokens.pop(token, None)
        raise HTTPException(status_code=401, detail="管理员登录已失效，请重新登录。")


async def save_uploads(request: Request, conversation_id: str, files: list[UploadFile]) -> list[dict[str, Any]]:
    attachments: list[dict[str, Any]] = []
    if not files:
        return attachments

    conversation_dir = UPLOADS_DIR / conversation_id
    conversation_dir.mkdir(parents=True, exist_ok=True)

    for upload in files:
        if not upload.filename:
            continue
        attachment_id = uuid4().hex
        safe_name = sanitize_filename(upload.filename)
        stored_name = f"{attachment_id}__{safe_name}"
        target = conversation_dir / stored_name
        content = await upload.read()
        if len(content) > MAX_ATTACHMENT_SIZE_BYTES:
            raise HTTPException(status_code=413, detail=f"附件 {safe_name} 超过 100MB 限制。")
        target.write_bytes(content)
        attachments.append(
            {
                "id": attachment_id,
                "name": safe_name,
                "mime_type": normalize_attachment_mime_type(upload.content_type, safe_name),
                "size_bytes": len(content),
                "stored_name": stored_name,
                "url": attachment_public_path(conversation_id, attachment_id, safe_name),
            }
        )
    return attachments


app = FastAPI(title="DIT Feedback Server", version="0.1.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/")
async def admin_index() -> FileResponse:
    return FileResponse(ADMIN_DIR / "index.html")


@app.get("/styles.css")
async def admin_styles() -> FileResponse:
    return FileResponse(ADMIN_DIR / "styles.css")


@app.get("/app.js")
async def admin_script() -> FileResponse:
    return FileResponse(ADMIN_DIR / "app.js")


@app.post("/api/client/session")
async def create_or_restore_client_session(request: Request, payload: ClientSessionPayload) -> JSONResponse:
    conversation, messages, metadata_changed = database.get_or_create_client_conversation(request, payload)
    if metadata_changed:
        await hub.broadcast_conversation(conversation)
    return JSONResponse({"conversation": conversation, "messages": messages})


@app.get("/api/client/conversations/{conversation_id}/messages")
async def client_messages(
    request: Request,
    conversation_id: str,
    client_id: str = Query(...),
    client_token: str = Query(...),
) -> JSONResponse:
    conversation, messages = database.list_client_messages(request, conversation_id, client_id, client_token)
    return JSONResponse({"conversation": conversation, "messages": messages})


@app.post("/api/client/messages")
async def post_client_message(
    request: Request,
    conversation_id: str = Form(...),
    client_id: str = Form(...),
    client_token: str = Form(...),
    text: str = Form(""),
    app_version: str = Form(""),
    system_summary: str = Form(""),
    project_name: str = Form(""),
    project_path: str = Form(""),
    files: list[UploadFile] = File(default=[]),
) -> JSONResponse:
    database.authenticate_client(conversation_id, client_id, client_token)
    attachments = await save_uploads(request, conversation_id, files)
    conversation, message = database.append_message(
        request,
        conversation_id,
        "client",
        text,
        attachments,
        app_version=app_version,
        system_summary=system_summary,
        project_name=project_name,
        project_path=project_path,
    )
    await hub.broadcast_message(conversation, message)
    return JSONResponse({"conversation": conversation, "message": message})


@app.delete("/api/client/conversations/{conversation_id}/messages/{message_id}")
async def delete_client_message(
    request: Request,
    conversation_id: str,
    message_id: int,
    client_id: str = Query(...),
    client_token: str = Query(...),
) -> JSONResponse:
    conversation, deleted_message_id = database.delete_client_message(
        request,
        conversation_id,
        message_id,
        client_id,
        client_token,
    )
    await hub.broadcast_message_deleted(conversation, deleted_message_id)
    return JSONResponse({"conversation": conversation, "message_id": deleted_message_id})


@app.delete("/api/client/conversations/{conversation_id}/messages")
async def clear_client_messages(
    request: Request,
    conversation_id: str,
    client_id: str = Query(...),
    client_token: str = Query(...),
) -> JSONResponse:
    conversation, deleted_message_ids = database.clear_client_messages(
        request,
        conversation_id,
        client_id,
        client_token,
    )
    if deleted_message_ids:
        await hub.broadcast_messages_cleared(conversation, deleted_message_ids)
    return JSONResponse(
        {
            "conversation": conversation,
            "message_ids": deleted_message_ids,
            "deleted_count": len(deleted_message_ids),
        }
    )


@app.post("/api/admin/login")
async def admin_login(payload: AdminLoginPayload) -> JSONResponse:
    if payload.username != ADMIN_USERNAME or payload.password != ADMIN_PASSWORD:
        raise HTTPException(status_code=401, detail="管理员账号或密码错误。")
    token = secrets.token_urlsafe(32)
    expires_at = datetime.now(timezone.utc) + timedelta(hours=ADMIN_TOKEN_TTL_HOURS)
    admin_tokens[token] = AdminToken(token=token, expires_at=expires_at)
    return JSONResponse({"token": token, "expires_at": expires_at.isoformat()})


@app.get("/api/admin/conversations")
async def admin_conversations(request: Request, authorization: str | None = Header(default=None)) -> JSONResponse:
    verify_admin_token(parse_bearer_token(authorization))
    return JSONResponse({"conversations": database.list_conversations(request)})


@app.get("/api/admin/conversations/{conversation_id}/messages")
async def admin_messages(request: Request, conversation_id: str, authorization: str | None = Header(default=None)) -> JSONResponse:
    verify_admin_token(parse_bearer_token(authorization))
    conversation, messages = database.list_admin_messages(request, conversation_id)
    return JSONResponse({"conversation": conversation, "messages": messages})


@app.post("/api/admin/conversations/{conversation_id}/messages")
async def post_admin_message(
    request: Request,
    conversation_id: str,
    authorization: str | None = Header(default=None),
    text: str = Form(""),
    files: list[UploadFile] = File(default=[]),
) -> JSONResponse:
    verify_admin_token(parse_bearer_token(authorization))
    attachments = await save_uploads(request, conversation_id, files)
    conversation, message = database.append_message(request, conversation_id, "admin", text, attachments)
    await hub.broadcast_message(conversation, message)
    return JSONResponse({"conversation": conversation, "message": message})


@app.post("/api/admin/conversations/{conversation_id}/status")
async def update_admin_status(
    request: Request,
    conversation_id: str,
    payload: AdminStatusPayload,
    authorization: str | None = Header(default=None),
) -> JSONResponse:
    verify_admin_token(parse_bearer_token(authorization))
    conversation = database.update_status(request, conversation_id, payload.status)
    await hub.broadcast_conversation(conversation)
    return JSONResponse({"conversation": conversation})


@app.delete("/api/admin/conversations/{conversation_id}")
async def delete_admin_conversation(
    conversation_id: str,
    authorization: str | None = Header(default=None),
) -> JSONResponse:
    verify_admin_token(parse_bearer_token(authorization))
    deletion = database.delete_conversation(conversation_id)
    await hub.broadcast_conversation_deleted(deletion["conversation_id"], deletion["client_id"])
    return JSONResponse(deletion)


@app.get("/files/{conversation_id}/{attachment_id}/{filename}")
async def download_attachment(conversation_id: str, attachment_id: str, filename: str) -> FileResponse:
    safe_name = sanitize_filename(filename)
    target = UPLOADS_DIR / conversation_id / f"{attachment_id}__{safe_name}"
    if not target.exists():
        raise HTTPException(status_code=404, detail="附件不存在。")
    return FileResponse(target, filename=safe_name)


@app.websocket("/ws/admin")
async def admin_ws(websocket: WebSocket, token: str = Query(...)) -> None:
    try:
        verify_admin_token(token)
    except HTTPException:
        await websocket.close(code=4401, reason="管理员认证失败")
        return

    await hub.connect_admin(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        hub.disconnect_admin(websocket)
    except Exception:
        hub.disconnect_admin(websocket)


@app.websocket("/ws/client/{client_id}")
async def client_ws(websocket: WebSocket, client_id: str, token: str = Query(...)) -> None:
    expected_token = database.get_client_token(client_id)
    if not expected_token or token != expected_token:
        await websocket.close(code=4401, reason="客户端认证失败")
        return

    await hub.connect_client(client_id, websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        hub.disconnect_client(client_id, websocket)
    except Exception:
        hub.disconnect_client(client_id, websocket)
