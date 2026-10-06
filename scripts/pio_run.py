#!/usr/bin/env python3
"""Build a synchronized source copy because ESP-IDF rejects paths with spaces."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def stage_project(root):
    suffix = hashlib.sha256(str(root).encode()).hexdigest()[:12]
    stage = Path(tempfile.gettempdir()) / f"szp-s3-{os.getuid()}-{suffix}"
    stage.mkdir(mode=0o700, exist_ok=True)
    if stage.is_symlink() or stage.stat().st_uid != os.getuid():
        raise RuntimeError(f"Unsafe build staging directory: {stage}")
    sources = [root / name for name in (
        "platformio.ini", "CMakeLists.txt", "partitions.csv", "sdkconfig.defaults"
    )]
    if (root / "dependencies.lock").exists():
        sources.append(root / "dependencies.lock")
    for directory in ("src", "include", "components", "boards"):
        base = root / directory
        if base.exists():
            sources.extend(path for path in base.rglob("*") if path.is_file())
    defaults = root / "sdkconfig.defaults"
    staged_defaults = stage / "sdkconfig.defaults"
    if staged_defaults.exists() and defaults.read_bytes() != staged_defaults.read_bytes():
        config = stage / "sdkconfig.szp_s3"
        if config.exists():
            config.unlink()
    for source in sources:
        target = stage / source.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or source.read_bytes() != target.read_bytes():
            shutil.copy2(source, target)
    # Remove stale source files only inside our owned staging directories.
    wanted = {str(source.relative_to(root)) for source in sources}
    for directory in ("src", "include", "components", "boards"):
        base = stage / directory
        if base.exists():
            for target in base.rglob("*"):
                if target.is_file() and str(target.relative_to(stage)) not in wanted:
                    target.unlink()
    return stage


def main():
    root = Path(__file__).resolve().parents[1]
    pio = Path(sys.executable).parent / "pio"
    action = sys.argv[1] if len(sys.argv) > 1 else "build"
    os.environ["ESP_IDF_VERSION"] = "5.5.0"  # codec component Kconfig compatibility gate
    stage = stage_project(root)
    command = [str(pio)]
    if action in ("build", "flash", "flash-usb"):
        command += ["run", "-e", "szp_s3"]
        if action != "build":
            command += ["-t", "upload"]
        if action == "flash-usb":
            command += ["--upload-port", "/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_CC:BA:97:0A:4E:5C-if00"]
    elif action == "monitor":
        command += ["device", "monitor", "-e", "szp_s3"]
    elif action == "debug":
        command += ["debug", "-e", "szp_s3", "--interface=gdb"]
    else:
        raise ValueError(f"Unknown action: {action}")
    print(f"Source: {root}\nBuild workspace: {stage}", flush=True)
    artifacts = root / "artifacts"
    artifacts.mkdir(exist_ok=True)
    (artifacts / "build-workspace.txt").write_text(str(stage) + "\n")
    if action in ("debug", "monitor"):
        return subprocess.call(command, cwd=stage)
    with (artifacts / f"{action}.log").open("w") as log:
        process = subprocess.Popen(command, cwd=stage, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, bufsize=1)
        try:
            for line in process.stdout:
                print(line, end="", flush=True)
                log.write(line)
                log.flush()
            result = process.wait()
        except KeyboardInterrupt:
            process.terminate()
            process.wait()
            return 130
    if result == 0:
        destination = root / ".pio/build/szp_s3"
        destination.mkdir(parents=True, exist_ok=True)
        for name in ("firmware.elf", "firmware.bin", "bootloader.bin", "partitions.bin"):
            source = stage / ".pio/build/szp_s3" / name
            if source.exists():
                shutil.copy2(source, destination / name)
        config = stage / "sdkconfig.szp_s3"
        if config.exists():
            shutil.copy2(config, root / config.name)
        dependency_lock = stage / "dependencies.lock"
        if dependency_lock.exists():
            shutil.copy2(dependency_lock, root / dependency_lock.name)
    return result


if __name__ == "__main__":
    raise SystemExit(main())

