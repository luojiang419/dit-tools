from __future__ import annotations

import json
import re
import subprocess
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit


WEB_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = WEB_ROOT.parent


class ResourceParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.references: list[str] = []
        self.ids: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        if values.get("id"):
            self.ids.append(str(values["id"]))
        for name in ("src", "href"):
            value = values.get(name)
            if value:
                self.references.append(str(value))


def parse_html(path: Path) -> ResourceParser:
    parser = ResourceParser()
    parser.feed(path.read_text(encoding="utf-8"))
    return parser


def test_html_references_and_ids_are_valid() -> None:
    for path in WEB_ROOT.rglob("*.html"):
        parser = parse_html(path)
        assert len(parser.ids) == len(set(parser.ids)), f"重复 id: {path}"
        for reference in parser.references:
            parsed = urlsplit(reference)
            if parsed.scheme or parsed.netloc or reference.startswith(("#", "mailto:", "tel:")):
                continue
            target = (path.parent / parsed.path).resolve()
            if reference.endswith("/"):
                target /= "index.html"
            assert target.exists(), f"缺失资源: {path} -> {reference}"


def test_javascript_syntax() -> None:
    for path in WEB_ROOT.rglob("*.js"):
        result = subprocess.run(
            ["node", "--check", str(path)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        assert result.returncode == 0, result.stderr


def test_demo_matches_qml_layout_contract_and_has_no_data_transport() -> None:
    main_qml = (REPO_ROOT / "dit-tools-src/cinevault-pro/src/ui/qml/Main.qml").read_text(encoding="utf-8")
    top_qml = (REPO_ROOT / "dit-tools-src/cinevault-pro/src/ui/qml/components/TopCommandBar.qml").read_text(encoding="utf-8")
    demo_css = (WEB_ROOT / "demo/demo.css").read_text(encoding="utf-8")
    demo_html = (WEB_ROOT / "demo/index.html").read_text(encoding="utf-8")
    demo_js = (WEB_ROOT / "demo/demo.js").read_text(encoding="utf-8")

    assert re.search(r"\bwidth:\s*1600\b", main_qml)
    assert re.search(r"\bheight:\s*980\b", main_qml)
    assert re.search(r"implicitHeight:\s*64\b", top_qml)
    assert "width: 1600px" in demo_css
    assert "height: 980px" in demo_css
    assert ".top-command-bar { height: 64px" in demo_css
    assert "flex: 0 0 270px" in demo_css
    assert "flex: 0 0 330px" in demo_css
    assert 'type="file"' not in demo_html.lower()
    assert "fetch(" not in demo_js
    assert "XMLHttpRequest" not in demo_js
    assert "WebSocket" not in demo_js
    assert "演示模式 · 不访问本机文件" in demo_html


def test_release_manifest_is_complete_and_matches_page_fallback() -> None:
    release = json.loads((WEB_ROOT / "downloads/release.json").read_text(encoding="utf-8"))
    assert re.fullmatch(r"v\d+\.\d+\.\d+", release["version"])
    assert release["fileName"].endswith(".exe")
    assert release["sizeBytes"] > 100_000_000
    assert re.fullmatch(r"[0-9a-f]{64}", release["sha256"])
    assert release["serverUrl"].endswith(release["fileName"])
    assert release["fallbackUrl"].startswith("https://github.com/")
    page = (WEB_ROOT / "index.html").read_text(encoding="utf-8")
    assert release["fallbackUrl"] in page


def test_frontend_and_backend_share_api_namespace() -> None:
    namespace = "/api/cinevault-support/v1"
    assert namespace in (WEB_ROOT / "support-shared.js").read_text(encoding="utf-8")
    assert namespace in (WEB_ROOT / "sponsors-admin.js").read_text(encoding="utf-8")
    assert namespace in (WEB_ROOT / "server/app/main.py").read_text(encoding="utf-8")


def test_download_modal_combines_free_download_support_and_contact_routes() -> None:
    page = (WEB_ROOT / "index.html").read_text(encoding="utf-8")
    script = (WEB_ROOT / "script.js").read_text(encoding="utf-8")
    assert "免费下载 · 自愿支持" in page
    assert "不赞助不会影响下载" in page
    assert "data-download-link" in page
    assert "data-modal-support-form" in page
    assert "assets/support/wechat-support.jpg" in page
    assert "assets/support/alipay-support.jpg" in page
    assert "assets/support/qq-group-912211398.jpg" in page
    assert "15085152352" in page
    assert "419773176@qq.com" in page
    assert "912211398" in page
    assert "support.submitClaim(modalSupportForm)" in script
    assert 'pageParams.get("download") === "1"' in script


def test_no_legacy_product_namespace_leaked_into_new_site() -> None:
    legacy_name = "pond" + "5"
    allowed_binary_suffixes = {".png", ".jpg", ".jpeg", ".ico", ".exe", ".pyc"}
    for path in WEB_ROOT.rglob("*"):
        if not path.is_file() or path.suffix.lower() in allowed_binary_suffixes:
            continue
        assert legacy_name not in path.read_text(encoding="utf-8", errors="ignore").lower(), path
