"""Isolated SQLite complexity probe. Extracts the current UPDATE from ScanEngine.

Only temporary synthetic databases are touched. Not a live GUI/scan benchmark.
"""
import argparse
import hashlib
import json
import platform
import re
import sqlite3
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "dit-tools-src/cinevault-pro/src/core/scan/ScanEngine.cpp"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    source = SOURCE.read_text(encoding="utf-8-sig")
    section = source.split("refreshFolderCounts.prepare(QStringLiteral(", 1)[1].split("));", 1)[0]
    sql = "".join(json.loads(token) for token in re.findall(r'"(?:[^"\\]|\\.)*"', section))
    report = {"scope": "isolated partial-scan folder-count UPDATE; synthetic schema with relevant current indexes",
              "python": platform.python_version(), "sqlite": sqlite3.sqlite_version,
              "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(), "sql": sql, "cases": []}
    for total, affected in [(10000, 10), (100000, 1), (100000, 10), (100000, 50), (300000, 10)]:
        with tempfile.TemporaryDirectory(prefix="cinevault-audit-") as temp:
            db = sqlite3.connect(str(Path(temp) / "scale.sqlite"))
            db.executescript("""
                PRAGMA journal_mode=WAL;
                CREATE TABLE asset_file(id INTEGER PRIMARY KEY, source_root_id INTEGER,
                    name TEXT, relative_path TEXT, path_key TEXT);
                CREATE INDEX idx_asset_file_source_root_id ON asset_file(source_root_id);
                CREATE INDEX idx_asset_file_name ON asset_file(name);
                CREATE INDEX idx_asset_file_source_path_key ON asset_file(source_root_id,path_key);
                CREATE TABLE folder_node(id INTEGER PRIMARY KEY, source_root_id INTEGER,
                    relative_path TEXT, parent_relative_path TEXT, depth INTEGER,
                    direct_file_count INTEGER, file_count INTEGER, recursive_file_count INTEGER, updated_at TEXT);
                CREATE INDEX idx_folder_node_source_root_id ON folder_node(source_root_id);
                CREATE INDEX idx_folder_node_parent ON folder_node(source_root_id,parent_relative_path,depth);
            """)
            db.executemany("INSERT INTO asset_file VALUES(?,?,?,?,?)",
                           ((i + 1, 1, f"f{i}.mov", f"d{i % 1000}/f{i}.mov", f"d{i % 1000}/f{i}.mov") for i in range(total)))
            db.executemany("INSERT INTO folder_node VALUES(?,?,?,?,?,?,?,?,?)",
                           ((i + 1, 1, f"d{i}", "", 1, 0, 0, 0, "") for i in range(1000)))
            db.commit()
            values = lambda path: (1, path, 1, path, 1, path, path, path, path, "audit", 1, path)
            plan = list(db.execute("EXPLAIN QUERY PLAN " + sql, values("d0")))
            started = time.perf_counter()
            db.execute("BEGIN")
            for index in range(affected):
                db.execute(sql, values(f"d{index}"))
            db.commit()
            elapsed = (time.perf_counter() - started) * 1000
            counts = list(db.execute("SELECT direct_file_count,recursive_file_count FROM folder_node WHERE id<=?", (affected,)))
            assert all(a == total // 1000 and b == a for a, b in counts), counts
            # Illustrative one-pass aggregate for this flat fixture only; not a full tree migration.
            started = time.perf_counter()
            grouped = dict(db.execute("""SELECT CASE WHEN length(relative_path)>length(name)
                THEN substr(relative_path,1,length(relative_path)-length(name)-1) ELSE '' END,
                COUNT(*) FROM asset_file WHERE source_root_id=1 GROUP BY 1"""))
            group_ms = (time.perf_counter() - started) * 1000
            assert len(grouped) == 1000 and sum(grouped.values()) == total
            report["cases"].append({"assets": total, "affected_folders": affected,
                                    "current_update_ms": round(elapsed, 2),
                                    "illustrative_flat_group_query_ms": round(group_ms, 2),
                                    "counts_verified": True, "query_plan": plan})
            db.close()
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
