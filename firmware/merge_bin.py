"""Post-build step: produce the two artefacts people actually flash.

Runs on every `pio run -e cardputer`, so neither is a manual step that gets
forgotten before a release.

There are two, because there are two ways to flash a Cardputer and they need
*different* files:

  m5-smarthome.bin              the application alone. This is what an SD-card
                                launcher (M5Launcher, Bruce) installs, and what
                                OTA takes. It carries an app descriptor at
                                offset 0x20 and is written into an app
                                partition.

  m5-smarthome-esptool-full.bin bootloader + partition table + OTA selector +
                                application, at their correct offsets, written
                                from 0x0 with esptool or M5Burner. It replaces
                                everything on the device.

Handing the full image to a launcher is the mistake this file exists to make
hard: the launcher writes it into an app partition, where a bootloader is not
a valid application, and the device comes up with nothing to run.

A launcher partition on this hardware is 1536 KB, so the build fails rather
than quietly producing an application that can no longer be installed that way.
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

# What an SD-card launcher offers as an app slot on this hardware. Exceeding
# it does not break the esptool path, but it silently removes the way most
# people install firmware on a Cardputer.
LAUNCHER_SLOT_BYTES = 1536 * 1024

# esp_app_desc_t magic, found at offset 0x20 of a valid application image.
APP_DESC_MAGIC = 0xABCD5432


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


def sha256_file(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def write_digest(path):
    digest = sha256_file(path)
    with open(path + ".sha256", "w") as fh:
        fh.write("%s  %s\n" % (digest, os.path.basename(path)))
    return digest


def check_is_app_image(path):
    """A launcher writes this into an app partition; a bootloader there bricks
    the boot. Cheap to verify, so verify it rather than trust the filename."""
    with open(path, "rb") as fh:
        head = fh.read(0x24)
    if len(head) < 0x24 or head[0] != 0xE9:
        return False
    magic = int.from_bytes(head[0x20:0x24], "little")
    return magic == APP_DESC_MAGIC


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

    # --- the application, for launchers and OTA --------------------------
    app_size = os.path.getsize(app)
    if not check_is_app_image(app):
        raise SystemExit(
            "merge_bin: firmware.bin is not a valid application image "
            "(no app descriptor at 0x20). Refusing to publish it as one.")
    if app_size > LAUNCHER_SLOT_BYTES:
        raise SystemExit(
            "merge_bin: the application is %d bytes, over the %d-byte app slot "
            "an SD-card launcher offers. It would still flash over USB, but "
            "installing it the way most Cardputer firmware is installed would "
            "stop working. Shrink it, or raise LAUNCHER_SLOT_BYTES knowingly."
            % (app_size, LAUNCHER_SLOT_BYTES))

    launcher_bin = os.path.join(build_dir, "m5-smarthome.bin")
    with open(app, "rb") as src, open(launcher_bin, "wb") as dst:
        dst.write(src.read())
    write_digest(launcher_bin)
    print("merge_bin: %s (%d bytes, %d%% of the %d KB launcher slot)"
          % (launcher_bin, app_size, 100 * app_size // LAUNCHER_SLOT_BYTES,
             LAUNCHER_SLOT_BYTES // 1024))

    # --- the whole image, for esptool and M5Burner ------------------------
    out = os.path.join(build_dir, "m5-smarthome-esptool-full.bin")
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
        write_digest(out)
        print("merge_bin: %s (%d bytes)" % (out, os.path.getsize(out)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)   # noqa: F821
