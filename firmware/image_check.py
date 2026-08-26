"""Pure checks on ESP32 image files.

Split out of merge_bin.py so they can be tested without PlatformIO: that file
runs inside a SCons environment and cannot simply be imported. These functions
take bytes and return facts, which is the testable half.

The distinction they encode cost a real device a failed install: a launcher
writes the file it is given into an *app partition*, so it must be an
application. Both an application and a full flash image start with 0xE9, so
the first byte cannot tell them apart — the app descriptor at offset 0x20 can.
"""

import struct

#: Magic of esp_app_desc_t, which sits at offset 0x20 of an application image.
APP_DESC_MAGIC = 0xABCD5432

#: ESP image header magic, shared by bootloaders and applications alike.
IMAGE_MAGIC = 0xE9

#: esp_chip_id_t for the ESP32-S3, at offset 12 of the image header.
CHIP_ID_ESP32S3 = 9

#: The app slot an SD-card launcher offers on this hardware.
LAUNCHER_SLOT_BYTES = 1536 * 1024

#: Partitions are aligned, so a file occupies more than its own length.
PARTITION_ALIGNMENT = 64 * 1024


def is_esp_image(head: bytes) -> bool:
    return len(head) >= 1 and head[0] == IMAGE_MAGIC


def is_app_image(head: bytes) -> bool:
    """True only for an application image, not for a merged flash image."""
    if not is_esp_image(head) or len(head) < 0x24:
        return False
    (magic,) = struct.unpack("<I", head[0x20:0x24])
    return magic == APP_DESC_MAGIC


def chip_id(head: bytes):
    """esp_chip_id_t from the extended header, or None if absent."""
    if not is_esp_image(head) or len(head) < 14:
        return None
    (cid,) = struct.unpack("<H", head[12:14])
    return cid


def segment_count(head: bytes):
    return head[1] if is_esp_image(head) and len(head) >= 2 else None


def align_up(value: int, alignment: int = PARTITION_ALIGNMENT) -> int:
    if alignment <= 0:
        raise ValueError("alignment must be positive")
    return ((value + alignment - 1) // alignment) * alignment


def fits_launcher_slot(size: int, slot: int = LAUNCHER_SLOT_BYTES) -> bool:
    """A launcher needs the *aligned* size to fit, not the raw file length.

    Checking the raw length would pass a file that the launcher then refuses,
    which is the worst place to find out.
    """
    return align_up(size) <= slot


def slot_usage_percent(size: int, slot: int = LAUNCHER_SLOT_BYTES) -> int:
    if slot <= 0:
        raise ValueError("slot must be positive")
    return 100 * size // slot


def describe(head: bytes, size: int) -> dict:
    """Everything the build and CI want to assert about one file."""
    return {
        "esp_image": is_esp_image(head),
        "app_image": is_app_image(head),
        "chip_id": chip_id(head),
        "segments": segment_count(head),
        "size": size,
        "aligned_size": align_up(size),
        "fits_launcher_slot": fits_launcher_slot(size),
        "slot_percent": slot_usage_percent(size),
    }
