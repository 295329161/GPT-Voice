#!/usr/bin/env python3
"""Build a synchronized source copy because ESP-IDF rejects paths with spaces."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

NATIVE_PORT = Path('/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_CC:BA:97:0A:4E:5C-if00')
UART_PORT = Path('/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0')


def usb_storage_active(sysfs=Path('/sys/bus/usb/devices')):
    """Do not reset a card that may still be mounted by the computer."""
    for device in sysfs.iterdir():
        try:
            if (device / 'idVendor').read_text().strip() == '303a' and \
               (device / 'idProduct').read_text().strip() == '4002' and \
               (device / 'serial').read_text().strip() == 'CCBA970A4E5C':
                return True
        except (FileNotFoundError, NotADirectoryError):
            continue
    return False


def native_or_uart(native=NATIVE_PORT, uart=UART_PORT):
    if native.exists():
        return native
    if uart.exists():
        print('Native USB unavailable; using the independent CH340 upload port.', flush=True)
        return uart
    raise RuntimeError('Device not connected: neither native USB nor CH340 is available')


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
    # PlatformIO watches only the top-level CMake files. Component manifests
    # and newly added local overrides also require CMake dependency resolution.
    cmake_inputs = hashlib.sha256()
    for source in sorted(sources):
        if source.name in ("CMakeLists.txt", "idf_component.yml", "Kconfig", "Kconfig.projbuild", "dependencies.lock"):
            cmake_inputs.update(str(source.relative_to(root)).encode())
            cmake_inputs.update(source.read_bytes())
    stamp = stage / ".cmake-inputs.sha256"
    signature = cmake_inputs.hexdigest()
    source_lock, staged_lock = root / 'dependencies.lock', stage / 'dependencies.lock'
    lock_changed = source_lock.exists() and staged_lock.exists() and source_lock.read_bytes() != staged_lock.read_bytes()
    if not stamp.exists() or stamp.read_text() != signature or lock_changed:
        cache = stage / ".pio/build/szp_s3/CMakeCache.txt"
        if cache.exists():
            cache.unlink()
        stamp.write_text(signature)
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
    if action in ('flash', 'flash-usb', 'debug') and usb_storage_active():
        print('USB 传输仍在开启。请先在电脑上安全弹出 SD 卡，待设备恢复普通模式后再烧录/调试。', file=sys.stderr)
        return 2
    if action == "monitor":
        # The generic pyserial monitor changes modem lines during open. This
        # board wires them to BOOT/RESET, so use the control-line-safe console.
        return subprocess.call([shutil.which("python3") or sys.executable,
                                str(root / "scripts/device_console.py"),
                                str(root / "artifacts/device-session")], cwd=root)
    os.environ["ESP_IDF_VERSION"] = "5.5.0"  # codec component Kconfig compatibility gate
    stage = stage_project(root)
    command = [str(pio)]
    if action in ("build", "flash", "flash-usb"):
        command += ["run", "-e", "szp_s3"]
        if action != "build":
            command += ["-t", "upload"]
        if action == "flash-usb":
            command += ["--upload-port", str(native_or_uart())]
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
