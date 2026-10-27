"""Actual SQLite/history/close integration; run in Linux or WSL.

Use --negative to prove the former call order still fails the same scenario.
Only fixture databases and test binaries are created in a temporary directory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import resource
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("--negative", action="store_true")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
deps = Path(os.environ.get("SONIX_TEST_DEPS", "/root/sonix-performance-b1-20261026/candidate/sonix-player"))
args.output.mkdir(parents=True, exist_ok=True)
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
sources = [root / p for p in (
    "src/system/library/library.c", "src/system/playback/playhistory.c",
    "src/system/library/library.h", "src/system/playback/playhistory.h",
    "src/main.c", "src/system/device/system.c",
    "tests/playhistory_close_library.c", "tests/playhistory_close_test.c",
)]
pins = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
results = []

def function(source, name):
    found = re.search(r'^(?:static )?(?:void|bool|int)\s+' + name + r'\([^;{}]*?\)\s*\{', source, re.M)
    assert found, name
    end, depth = found.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[found.start():end]

storage = function((root/'src/system/device/system.c').read_text(), 'card_databases_detach')
assert storage.index('audio_stop();') < storage.index('library_prepare_close();') < storage.index('playhistory_flush();') < storage.index('library_close();')
shutdown = (root/'src/main.c').read_text().split('gui_shutdown_completions();')[-1]
assert shutdown.index('library_prepare_close();') < shutdown.index('playhistory_shutdown();') < shutdown.index('library_checkpoint_shutdown();')
opening = function((root/'src/system/library/library.c').read_text(), 'library_open')
assert 'atomic_store(&database_preparing_close, false);' in opening


def run(label, command, expected=0):
    started = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True, timeout=90, cwd=root)
    (args.output / (label + ".log")).write_text(result.stdout + result.stderr)
    assert result.returncode == expected, (label, result.returncode, result.stdout, result.stderr)
    results.append(dict(case=label, exit_code=result.returncode, expected_exit=expected,
                        elapsed=time.monotonic() - started))
    print(label, "PASS" if expected == 0 else "EXPECTED REGRESSION REPRODUCED", flush=True)


with tempfile.TemporaryDirectory(prefix="sonix-history-close-") as folder:
    folder = Path(folder)
    sqlite = folder / "sqlite.o"
    binary = folder / "history-close-test"
    run("sqlite", ["cc", "-O1", "-g", "-c", str(root / "src/system/db/sqlite3_impl.c"), "-o", str(sqlite)])
    flags = ["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-ffunction-sections", "-fdata-sections", "-I" + str(root), "-I" + str(deps),
             "-I" + str(deps / "lvgl"), "-DLV_CONF_INCLUDE_SIMPLE=1",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
             "-Wl,--gc-sections", "-pthread"]
    run("build", flags + [str(root / "tests/playhistory_close_library.c"),
                          str(root / "tests/playhistory_close_test.c"), str(sqlite), "-ldl", "-lm", "-o", str(binary)])
    cases = ["ordinary-close", "predrained-control", "replacement"]
    if args.negative:
        cases.append("missing-prepare")
    for case in cases:
        expected = 1 if case == "missing-prepare" else 0
        run(case, [str(binary), case, str(folder / (case + "-old.db")),
                   str(folder / (case + "-new.db"))], expected)

assert all(hashlib.sha256((root / p).read_bytes()).hexdigest() == digest for p, digest in pins.items())
(args.output / "result.json").write_text(json.dumps(dict(
    passed=True, negative_control=args.negative, sanitized=True, sources=pins, results=results), indent=2) + "\n")
