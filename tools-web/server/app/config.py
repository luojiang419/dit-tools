from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


def _read_env_file(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def _as_bool(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes", "on"}


@dataclass(frozen=True, slots=True)
class Settings:
    base_dir: Path
    host: str
    port: int
    db_path: Path
    sponsor_admin_token: str
    allowed_origins: tuple[str, ...]
    trust_proxy_headers: bool


def load_settings(base_dir: Path | None = None) -> Settings:
    resolved = (base_dir or Path(__file__).resolve().parents[1]).resolve()
    env_file = _read_env_file(resolved / ".env")

    def read(name: str, default: str) -> str:
        return os.environ.get(name, env_file.get(name, default))

    db_path = Path(read("CINEVAULT_SPONSOR_DB_PATH", str(resolved / "data" / "sponsors.sqlite3"))).resolve()
    db_path.parent.mkdir(parents=True, exist_ok=True)
    origins = tuple(item.strip() for item in read(
        "CINEVAULT_ALLOWED_ORIGINS",
        "http://127.0.0.1:4173,http://localhost:4173",
    ).split(",") if item.strip())
    return Settings(
        base_dir=resolved,
        host=read("CINEVAULT_WEB_HOST", "127.0.0.1"),
        port=int(read("CINEVAULT_WEB_PORT", "3413")),
        db_path=db_path,
        sponsor_admin_token=read("CINEVAULT_SPONSOR_ADMIN_TOKEN", "").strip(),
        allowed_origins=origins,
        trust_proxy_headers=_as_bool(read("CINEVAULT_TRUST_PROXY_HEADERS", "true")),
    )
