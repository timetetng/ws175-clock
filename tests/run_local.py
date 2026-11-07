"""Host-only checks. No serial, network, IDF setup, installation or flashing."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    "motion": ["tests/motion_test.c"],
    "music-wav": ["tests/music_wav_test.c"],
    "settings-model": ["tests/settings_model_test.c"],
    "toy-shell": ["main/toy_shell.c", "tests/toy_shell_test.c"],
    "connectivity-model": ["main/connectivity_model.c", "tests/connectivity_model_test.c"],
}
with tempfile.TemporaryDirectory(prefix="ws175-native-") as folder:
    for name, sources in CASES.items():
        executable = str(Path(folder) / name)
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L",
            "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", *sources, "-lm", "-o", executable,
        ], cwd=ROOT, check=True)
        subprocess.run([executable], cwd=ROOT, check=True)
        print(f"PASS {name}", flush=True)
subprocess.run([sys.executable, "tests/frog_assets_test.py"], cwd=ROOT, check=True)
print("PASS: 5 native suites with ASan/UBSan + frog asset validation")
