#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.11"
# ///
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).parent
VCPKG_DIR = ROOT / "vcpkg"
IS_WINDOWS = os.name == "nt"
BOOTSTRAP_SCRIPT = VCPKG_DIR / ("bootstrap-vcpkg.bat" if IS_WINDOWS else "bootstrap-vcpkg.sh")
VCPKG_TOOLCHAIN = VCPKG_DIR / "scripts" / "buildsystems" / "vcpkg.cmake"


def ensure_vcpkg():
    # submodule not checked out yet — initialize it
    if not BOOTSTRAP_SCRIPT.is_file():
        print("vcpkg submodule not initialized, running git submodule update --init --recursive")
        subprocess.run(
            ["git", "submodule", "update", "--init", "--recursive"],
            cwd=ROOT,
            check=True,
        )

    if not BOOTSTRAP_SCRIPT.is_file():
        print(
            f"error: {BOOTSTRAP_SCRIPT} still not found after submodule init",
            file=sys.stderr,
        )
        sys.exit(1)

    # toolchain file appears after bootstrap has been run
    if not VCPKG_TOOLCHAIN.is_file():
        print(f"bootstrapping vcpkg via {BOOTSTRAP_SCRIPT.name}")
        if IS_WINDOWS:
            subprocess.run([str(BOOTSTRAP_SCRIPT)], cwd=VCPKG_DIR, check=True, shell=True)
        else:
            subprocess.run(["bash", str(BOOTSTRAP_SCRIPT)], cwd=VCPKG_DIR, check=True)


def build(config="release"):
    ensure_vcpkg()

    build_dir = ROOT / "build" / config

    configure_args = [
        "cmake",
        "-B",
        str(build_dir),
        f"-DCMAKE_BUILD_TYPE={config}",
        f"-DCMAKE_TOOLCHAIN_FILE={VCPKG_TOOLCHAIN}",
    ]

    subprocess.run(configure_args, cwd=ROOT, check=True)
    subprocess.run(
        ["cmake", "--build", str(build_dir), "--config", config],
        cwd=ROOT,
        check=True,
    )


if __name__ == "__main__":
    build()
