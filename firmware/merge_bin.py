"""Post-build step: produce one flashable image next to firmware.bin.

Runs on every `pio run -e cardputer`, so the artefact people actually want is
never a separate manual step that gets forgotten before a release.

The ESP32-S3 needs four pieces at four offsets. Getting one of them wrong
gives a device that boots into a loop with no useful message, so they are
merged here once, correctly, instead of being written out in a README for
everybody to retype.
"""

import glob
import hashlib
import os

Import("env")                                    # noqa: F821 — injected by PlatformIO

# Offsets are fixed by the ESP32-S3 boot ROM and the Arduino partition layout.
BOOTLOADER_OFFSET = "0x0"
PARTITIONS_OFFSET = "0x8000"
BOOT_APP0_OFFSET = "0xe000"
APP_OFFSET = "0x10000"


def find_boot_app0():
    """Locate boot_app0.bin without hard-coding a package path.

    The framework directory differs between a local install and a CI runner,
    and a wrong guess here would silently produce an image that cannot select
    an OTA slot.
    """
    pkg = env.PioPlatform().get_package_dir("framework-arduinoespressif32")  # noqa: F821
    if pkg:
        candidate = os.path.join(pkg, "tools", "partitions", "boot_app0.bin")
        if os.path.isfile(candidate):
            return candidate
    hits = glob.glob(os.path.join(os.path.expanduser("~"), ".platformio",
                                  "packages", "framework-arduinoespressif32*",
                                  "tools", "partitions", "boot_app0.bin"))
    return hits[0] if hits else None


def merge(source, target, env):                  # noqa: A002 — PlatformIO signature
    build_dir = env.subst("$BUILD_DIR")
    app = os.path.join(build_dir, "firmware.bin")
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    boot_app0 = find_boot_app0()

    missing = [p for p in (app, bootloader, partitions) if not os.path.isfile(p)]
    if missing or not boot_app0:
        print("merge_bin: skipped, missing %s" % (missing or ["boot_app0.bin"]))
        return

    out = os.path.join(build_dir, "m5-smarthome-full.bin")
    env.Execute(" ".join([
        '"$PYTHONEXE"', '"$OBJCOPY"', "--chip", "esp32s3", "merge_bin",
        "-o", '"%s"' % out,
        "--flash_mode", "${__get_board_flash_mode(__env__)}",
        "--flash_freq", "${__get_board_f_flash(__env__)}",
        "--flash_size", "8MB",
        BOOTLOADER_OFFSET, '"%s"' % bootloader,
        PARTITIONS_OFFSET, '"%s"' % partitions,
        BOOT_APP0_OFFSET, '"%s"' % boot_app0,
        APP_OFFSET, '"%s"' % app,
    ]))

    if os.path.isfile(out):
        digest = hashlib.sha256(open(out, "rb").read()).hexdigest()
        with open(out + ".sha256", "w") as fh:
            fh.write("%s  %s\n" % (digest, os.path.basename(out)))
        print("merge_bin: %s (%d bytes)" % (out, os.path.getsize(out)))
        print("merge_bin: sha256 %s" % digest)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)   # noqa: F821
