# PlatformIO post-build script: merge bootloader + partitions + app into one .bin
# Usage: pio run -t merge -e cyd   ->  .pio/build/cyd/firmware-merged.bin (flash at 0x0)
# Adapted from MatixYo/ESP32-Plane-Radar (MIT) for the classic ESP32 (bootloader @ 0x1000).

Import("env")

import os
from os.path import join


def merge_firmware(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    progname = env.subst("${PROGNAME}")
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    esptool = join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    boot_app0 = join(framework_dir, "tools", "partitions", "boot_app0.bin")
    merged = join(build_dir, "firmware-merged.bin")
    mcu = env.BoardConfig().get("build.mcu", "esp32")
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")
    # Classic ESP32 / S2 boot from 0x1000; C3/S3/C6 from 0x0.
    boot_off = "0x1000" if mcu in ("esp32", "esp32s2") else "0x0"

    bootloader = join(build_dir, "bootloader.bin")
    partitions = join(build_dir, "partitions.bin")
    firmware = join(build_dir, f"{progname}.bin")

    for path in (bootloader, partitions, boot_app0, firmware):
        if not os.path.isfile(path):
            raise FileNotFoundError(f"Missing {path}")

    cmd = [
        env.subst("$PYTHONEXE"), esptool, "--chip", mcu, "merge_bin",
        "-o", merged, "--flash_mode", "dio", "--flash_freq", "40m",
        "--flash_size", flash_size,
        boot_off, bootloader, "0x8000", partitions, "0xe000", boot_app0,
        "0x10000", firmware,
    ]
    print(f"Merging flash image -> {merged}")
    env.Execute(" ".join(f'"{c}"' if " " in c else c for c in cmd))
    return None


env.AddCustomTarget(
    name="merge",
    dependencies="${BUILD_DIR}/${PROGNAME}.bin",
    actions=env.Action(merge_firmware, "Merging flash image for web flasher"),
    title="Merge firmware",
    description="Create firmware-merged.bin (bootloader + partitions + app)",
)
