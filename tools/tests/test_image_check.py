"""Image identification — the check that decides whether a launcher can
install what we publish.

Every case here traces back to a real failed install: a full flash image was
handed to an SD-card launcher, which wrote it into an app partition where a
bootloader is not a valid application, and the device came up with nothing to
run. Both file kinds start with 0xE9, so the first byte cannot tell them apart.
"""

import pathlib
import struct
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "firmware"))
import image_check as ic  # noqa: E402


def header(*, magic=ic.IMAGE_MAGIC, segments=7, chip=ic.CHIP_ID_ESP32S3,
           desc=ic.APP_DESC_MAGIC):
    """Build a 0x24-byte ESP image header."""
    h = bytearray(0x24)
    h[0] = magic
    h[1] = segments
    struct.pack_into("<H", h, 12, chip)
    if desc is not None:
        struct.pack_into("<I", h, 0x20, desc)
    return bytes(h)


# --- telling an application from a flash image ---------------------------

def test_an_application_is_recognised():
    assert ic.is_app_image(header()) is True


def test_a_full_flash_image_is_not_an_application():
    # A merged image starts with the bootloader; offset 0x20 is padding.
    assert ic.is_app_image(header(desc=0xFFFFFFFF)) is False


def test_the_first_byte_alone_never_decides():
    """Both kinds carry 0xE9. Accepting on that would ship the broken one."""
    flash = header(desc=0xFFFFFFFF)
    assert ic.is_esp_image(flash) is True      # looks fine...
    assert ic.is_app_image(flash) is False     # ...and is still the wrong file


def test_something_that_is_not_an_esp_image_at_all():
    for junk in (b"", b"MZ", b"<html>404</html>", b"\x00" * 64):
        assert ic.is_esp_image(junk) is False
        assert ic.is_app_image(junk) is False


def test_a_truncated_header_is_refused_not_guessed():
    full = header()
    for n in (1, 8, 0x1F, 0x23):
        assert ic.is_app_image(full[:n]) is False


def test_a_near_miss_descriptor_is_refused():
    # One bit off is not "close enough" for something written into flash.
    assert ic.is_app_image(header(desc=ic.APP_DESC_MAGIC ^ 1)) is False


def test_chip_id_and_segments_are_read():
    assert ic.chip_id(header(chip=ic.CHIP_ID_ESP32S3)) == 9
    assert ic.chip_id(header(chip=0)) == 0      # ESP32, wrong board
    assert ic.segment_count(header(segments=7)) == 7
    assert ic.chip_id(b"\xe9") is None          # too short to know


# --- fitting the launcher slot -------------------------------------------

def test_size_is_checked_after_alignment_not_before():
    """A launcher needs the *aligned* size to fit, not the raw file length.

    Note the real slot is itself a multiple of the alignment (1536 KB = 24 x
    64 KB), so for that slot the two checks happen to agree and this
    distinction is invisible. It becomes visible the moment a slot is not
    aligned, which is what this pins — checking the raw length there would
    pass a file the launcher then refuses.
    """
    unaligned_slot = 1_500_000
    size = 1_490_000
    assert size < unaligned_slot                       # raw length says yes
    assert ic.align_up(size) > unaligned_slot          # aligned says no
    assert ic.fits_launcher_slot(size, unaligned_slot) is False


def test_the_real_slot_boundary_is_exact():
    slot = ic.LAUNCHER_SLOT_BYTES
    assert slot % ic.PARTITION_ALIGNMENT == 0          # why the above is subtle
    assert ic.fits_launcher_slot(slot) is True
    assert ic.fits_launcher_slot(slot + 1) is False


def test_an_exactly_aligned_file_fits():
    assert ic.fits_launcher_slot(ic.LAUNCHER_SLOT_BYTES) is True


def test_the_current_firmware_size_class_fits_with_room():
    # ~1.05 MB in a 1.5 MB slot; if this ever fails the build should too.
    assert ic.fits_launcher_slot(1_106_144) is True
    assert ic.slot_usage_percent(1_106_144) == 70


def test_alignment_rounds_up_and_leaves_multiples_alone():
    a = ic.PARTITION_ALIGNMENT
    assert ic.align_up(1) == a
    assert ic.align_up(a) == a
    assert ic.align_up(a + 1) == 2 * a
    assert ic.align_up(0) == 0


def test_bad_alignment_is_an_error_not_a_division_by_zero():
    with pytest.raises(ValueError):
        ic.align_up(100, 0)
    with pytest.raises(ValueError):
        ic.slot_usage_percent(100, 0)


# --- the summary the build and CI assert on -------------------------------

def test_describe_reports_an_application():
    d = ic.describe(header(), 1_106_144)
    assert d["app_image"] is True
    assert d["chip_id"] == ic.CHIP_ID_ESP32S3
    assert d["fits_launcher_slot"] is True
    assert d["aligned_size"] >= d["size"]


def test_describe_reports_a_flash_image_as_not_installable():
    d = ic.describe(header(desc=0xFFFFFFFF), 1_171_680)
    assert d["esp_image"] is True
    assert d["app_image"] is False
