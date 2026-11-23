import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


TEMP_ROOT = Path(tempfile.mkdtemp(prefix="feedback-server-test-"))
os.environ["DIT_FEEDBACK_STORAGE_DIR"] = str(TEMP_ROOT)
os.environ["DIT_FEEDBACK_DB_PATH"] = str(TEMP_ROOT / "feedback.sqlite3")

SERVER_ROOT = Path(__file__).resolve().parents[1]
if str(SERVER_ROOT) not in sys.path:
    sys.path.insert(0, str(SERVER_ROOT))

from app.main import normalize_attachment_mime_type  # noqa: E402


def tearDownModule() -> None:
    shutil.rmtree(TEMP_ROOT, ignore_errors=True)


class AttachmentMimeTypeTest(unittest.TestCase):
    def test_keeps_specific_upload_content_type(self) -> None:
        self.assertEqual(
            normalize_attachment_mime_type("image/png", "preview.png"),
            "image/png",
        )

    def test_guesses_png_when_upload_content_type_missing(self) -> None:
        self.assertEqual(
            normalize_attachment_mime_type("", "preview.png"),
            "image/png",
        )

    def test_guesses_png_when_upload_content_type_is_octet_stream(self) -> None:
        self.assertEqual(
            normalize_attachment_mime_type("application/octet-stream", "preview.png"),
            "image/png",
        )

    def test_falls_back_to_octet_stream_for_unknown_extension(self) -> None:
        self.assertEqual(
            normalize_attachment_mime_type("", "preview.unknownext"),
            "application/octet-stream",
        )


if __name__ == "__main__":
    unittest.main()
