import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location("fetch_sdk", Path(__file__).resolve().parents[2] / "ci/fetch_asio_sdk.py")
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class AsioSdkDownload(unittest.TestCase):
    def fixture(self, files):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w") as archive:
            for name, data in files.items():
                archive.writestr(name, data)
        payload = stream.getvalue()
        manifest = {"version": "test", "url": "https://example.invalid/sdk.zip",
                    "archive_sha256": hashlib.sha256(payload).hexdigest(),
                    "files": {"common/asio.h": hashlib.sha256(b"test ABI").hexdigest()}}
        return payload, manifest

    def run_fetch(self, payload, manifest, path):
        with patch.object(sdk.urllib.request, "urlopen", return_value=io.BytesIO(payload)) as request:
            sdk.fetch_sdk(path, manifest)
            request.assert_called_once_with(manifest["url"], timeout=45)

    def test_verified_archive_is_extracted_and_validated(self):
        payload, manifest = self.fixture({"ASIOSDK/common/asio.h": b"test ABI"})
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "sdk"
            self.run_fetch(payload, manifest, root)
            self.assertEqual((root / "ASIOSDK/common/asio.h").read_bytes(), b"test ABI")

    def test_unexpected_archive_is_rejected_before_any_extraction(self):
        payload, manifest = self.fixture({"ASIOSDK/common/asio.h": b"test ABI"})
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "sdk"
            with self.assertRaisesRegex(ValueError, "Unexpected ASIO SDK archive"):
                self.run_fetch(payload + b"changed", manifest, root)
            self.assertFalse(root.exists())

    def test_archive_cannot_write_outside_destination(self):
        payload, manifest = self.fixture({"../outside.txt": b"escape"})
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "Unsafe SDK archive path"):
                self.run_fetch(payload, manifest, Path(temporary) / "sdk")
            self.assertFalse((Path(temporary) / "outside.txt").exists())

    def test_modified_header_is_rejected(self):
        payload, manifest = self.fixture({"ASIOSDK/common/asio.h": b"changed ABI"})
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "Unexpected ASIO SDK file"):
                self.run_fetch(payload, manifest, Path(temporary) / "sdk")

    def test_missing_header_is_rejected(self):
        payload, manifest = self.fixture({"ASIOSDK/other.txt": b"no header"})
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(FileNotFoundError):
                self.run_fetch(payload, manifest, Path(temporary) / "sdk")

    def test_network_error_is_propagated_without_creating_a_directory(self):
        _, manifest = self.fixture({})
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "sdk"
            with patch.object(sdk.urllib.request, "urlopen", side_effect=TimeoutError("timeout")):
                with self.assertRaises(TimeoutError):
                    sdk.fetch_sdk(root, manifest)
            self.assertFalse(root.exists())


if __name__ == "__main__":
    unittest.main()
