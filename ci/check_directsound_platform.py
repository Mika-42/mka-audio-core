"""A Linux configure must reject DirectSound before looking for native SDKs."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="mka-directsound-platform-") as directory:
    result = subprocess.run(["cmake", "-S", str(source), "-B", directory, "-G", "Ninja",
                             "-DMKA_BUILD_DIRECTSOUND=ON", "-DMKA_BUILD_TESTS=OFF"],
                            capture_output=True, text=True)
    assert result.returncode != 0, "DirectSound must not configure on Linux"
    assert "MKA_BUILD_DIRECTSOUND requires a Windows target" in result.stdout + result.stderr, result
print("DirectSound is rejected explicitly on a non-Windows target.")
