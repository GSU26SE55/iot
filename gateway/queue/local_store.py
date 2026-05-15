from __future__ import annotations

import json
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class QueueMessage:
    id: int
    kind: str
    payload: dict[str, Any]
    idempotency_key: str
    attempt_count: int
    next_attempt_at: datetime
    created_at: datetime
    last_error: str | None


class LocalQueue:
    def __init__(self, db_path: str | Path) -> None:
        self._db_path = Path(db_path)
        self._db_path.parent.mkdir(parents=True, exist_ok=True)
        self._init_db()

    def enqueue(self, *, kind: str, payload: dict[str, Any], idempotency_key: str) -> int:
        now = _utc_now()
        with self._connect() as conn:
            cursor = conn.execute(
                """
                INSERT INTO queue_messages
                    (kind, payload_json, idempotency_key, attempt_count, next_attempt_at, created_at, last_error)
                VALUES (?, ?, ?, 0, ?, ?, NULL)
                """,
                (kind, json.dumps(payload, separators=(",", ":")), idempotency_key, _to_iso(now), _to_iso(now)),
            )
            return int(cursor.lastrowid)

    def due_messages(self, limit: int = 20) -> list[QueueMessage]:
        now = _to_iso(_utc_now())
        with self._connect() as conn:
            rows = conn.execute(
                """
                SELECT id, kind, payload_json, idempotency_key, attempt_count, next_attempt_at, created_at, last_error
                FROM queue_messages
                WHERE next_attempt_at <= ?
                ORDER BY created_at ASC
                LIMIT ?
                """,
                (now, limit),
            ).fetchall()
        return [_row_to_message(row) for row in rows]

    def mark_sent(self, message_id: int) -> None:
        with self._connect() as conn:
            conn.execute("DELETE FROM queue_messages WHERE id = ?", (message_id,))

    def mark_failed(self, message_id: int, error: str, *, max_backoff_sec: int = 300) -> None:
        with self._connect() as conn:
            row = conn.execute(
                "SELECT attempt_count FROM queue_messages WHERE id = ?",
                (message_id,),
            ).fetchone()
            if row is None:
                return
            attempt_count = int(row[0]) + 1
            backoff = min(max_backoff_sec, 2 ** min(attempt_count, 8))
            next_attempt_at = _utc_now().timestamp() + backoff
            conn.execute(
                """
                UPDATE queue_messages
                SET attempt_count = ?, next_attempt_at = ?, last_error = ?
                WHERE id = ?
                """,
                (
                    attempt_count,
                    _to_iso(datetime.fromtimestamp(next_attempt_at, tz=timezone.utc)),
                    error[:500],
                    message_id,
                ),
            )

    def depth(self) -> int:
        with self._connect() as conn:
            row = conn.execute("SELECT COUNT(*) FROM queue_messages").fetchone()
            return int(row[0])

    def _init_db(self) -> None:
        with self._connect() as conn:
            conn.execute(
                """
                CREATE TABLE IF NOT EXISTS queue_messages (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    kind TEXT NOT NULL,
                    payload_json TEXT NOT NULL,
                    idempotency_key TEXT NOT NULL,
                    attempt_count INTEGER NOT NULL DEFAULT 0,
                    next_attempt_at TEXT NOT NULL,
                    created_at TEXT NOT NULL,
                    last_error TEXT NULL
                )
                """
            )
            conn.execute(
                """
                CREATE INDEX IF NOT EXISTS idx_queue_messages_due
                ON queue_messages(next_attempt_at, created_at)
                """
            )

    def _connect(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self._db_path)
        conn.row_factory = sqlite3.Row
        return conn


def _row_to_message(row: sqlite3.Row) -> QueueMessage:
    return QueueMessage(
        id=int(row["id"]),
        kind=str(row["kind"]),
        payload=json.loads(str(row["payload_json"])),
        idempotency_key=str(row["idempotency_key"]),
        attempt_count=int(row["attempt_count"]),
        next_attempt_at=_from_iso(str(row["next_attempt_at"])),
        created_at=_from_iso(str(row["created_at"])),
        last_error=row["last_error"],
    )


def _utc_now() -> datetime:
    return datetime.now(timezone.utc)


def _to_iso(value: datetime) -> str:
    return value.astimezone(timezone.utc).isoformat()


def _from_iso(value: str) -> datetime:
    parsed = datetime.fromisoformat(value)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)

