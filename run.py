#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.11"
# ///
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent


def main():
    # build first
    import build as b
    b.build()

    # run the benchmark
    config = "release"
    if sys.platform == "win32":
        exe = ROOT / "build" / config / "bin" / config.title() / "bench-thread-communication-latency.exe"
    else:
        exe = ROOT / "build" / config / "bin" / "bench-thread-communication-latency"
    subprocess.run([str(exe)], check=True)

    # generate charts from the results
    subprocess.run(["uv", "run", str(ROOT / "create-charts.py")], check=True)


if __name__ == "__main__":
    main()
