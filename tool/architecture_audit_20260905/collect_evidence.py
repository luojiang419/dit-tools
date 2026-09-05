"""Fingerprint audit inputs and outputs without reading user data or secrets."""
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "dit-tools-src/cinevault-pro"
PRODUCT_PATHS = [
    "src/app/AppContext.cpp", "src/application/IndexingWorkCoordinator.cpp",
    "src/application/VideoAnalysisService.cpp", "src/application/MaterialCatalogSyncService.cpp",
    "src/application/MaterialCenterQueryService.cpp", "src/application/MediaTaskService.cpp",
    "src/application/MetadataExtractionService.cpp", "src/application/SearchDocumentSyncService.cpp",
    "src/application/SourceChangeMonitor.cpp", "src/application/SourceChangeMonitor.h",
    "src/application/ImportService.cpp", "src/application/BackgroundMaintenanceCoordinator.cpp",
    "src/application/websearch/WebSearchService.cpp", "src/core/scan/ScanEngine.cpp",
    "src/core/scan/ScanPathPolicy.cpp", "src/core/jobs/JobEngine.cpp",
    "src/core/search/SearchEngine.cpp", "src/core/search/SemanticSearchIndexService.cpp",
    "src/core/search/SemanticVectorIndex.cpp", "src/domain/Entities.h", "src/domain/Enums.h",
    "src/infrastructure/db/DatabaseManager.cpp", "src/infrastructure/db/DatabaseMigration.cpp",
    "src/infrastructure/db/GlobalDatabaseManager.cpp", "src/infrastructure/ffmpeg/FFmpegAdapter.cpp",
    "src/infrastructure/ffmpeg/FFmpegAdapter.h", "src/shared/VisualAnalysisMetadata.cpp",
    "src/ui/viewmodels/MaterialCenterViewModel.cpp", "src/ui/viewmodels/LibraryWorkspaceViewModel.cpp",
    "src/ui/qml/workspaces/MaterialCenterWorkspace.qml",
    "tests/unit/FFmpegAdapterFrameExtractionTest.cpp", "tests/unit/MaterialCenterUiContractTest.cpp",
    "tests/stress/LargeCatalogStressTest.cpp",
]


def fingerprint(path):
    data = path.read_bytes()
    item = {"path": path.relative_to(ROOT).as_posix(), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}
    if path.suffix.lower() in {".cpp", ".h", ".py", ".txt", ".md", ".json"}:
        normalized = data.decode("utf-8-sig").replace("\r\n", "\n").encode("utf-8")
        item["normalized_utf8_lf_sha256"] = hashlib.sha256(normalized).hexdigest()
    return item


def main():
    git = lambda *args: subprocess.check_output(["git", *args], cwd=ROOT).decode("utf-8").strip()
    evidence = sorted((ROOT / "docs").glob("架构审计*2026-09-05.*"))
    evidence = [p for p in evidence if "证据清单" not in p.name]
    tools = sorted(p for p in Path(__file__).parent.iterdir() if p.is_file())
    binary = SRC / "build/architecture-audit-20260905"
    output = {"created_at": datetime.now(timezone.utc).isoformat(),
              "base_commit": git("rev-parse", "HEAD"), "branch": git("branch", "--show-current"),
              "working_version": (ROOT / "VERSION").read_text(encoding="utf-8-sig").strip(),
              "important": "Audited product sources include pre-existing uncommitted changes; audit commits contain only reports and diagnostic tools.",
              "product_sources": [fingerprint(SRC / p) for p in PRODUCT_PATHS],
              "evidence": [fingerprint(p) for p in evidence],
              "diagnostic_sources": [fingerprint(p) for p in tools],
              "diagnostic_binaries": [fingerprint(binary / name) for name in
                  ["ArchitectureAudit.exe", "CurrentFrameExtractionTest.exe", "CurrentSourceMonitorProbe.exe"]]}
    path = ROOT / "docs/架构审计证据清单-2026-09-05.json"
    path.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"fingerprinted {len(PRODUCT_PATHS)} product files and {len(evidence)} evidence files")


if __name__ == "__main__":
    main()
