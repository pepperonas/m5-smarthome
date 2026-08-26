"""The secret scanner.

Its reason to exist is one specific trap, so that trap is the first test:

    grep -qF "$PASSWORD" firmware.bin && echo LEAK

printed nothing here while the password sat in the file, and a scan of a
public artefact was reported clean on that basis.

Worth recording precisely, because the first diagnosis was wrong: BSD grep at
/usr/bin/grep gets this right. The `grep` actually running was ugrep, earlier
on PATH, which stays silent on a binary match. The lesson is not about one
implementation — it is that "whatever is called grep here" is not a defined
tool, so a security check must not be built on it.
"""

import importlib.util
import os
import pathlib
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("scan_secrets",
                                              ROOT / "tools" / "scan_secrets.py")
scan = importlib.util.module_from_spec(spec)
sys.modules["scan_secrets"] = scan
spec.loader.exec_module(scan)


NEEDLE = "hunter2-not-a-real-password"


@pytest.fixture
def binary_with_secret(tmp_path):
    """Binary noise, then the needle, with no trailing newline.

    Measured: for this shape even `grep -a` misses it, while `grep -q` misses
    it for every binary shape tried. Which of the two happens depends on the
    locale, the buffering and where the match falls — that unpredictability is
    the point, not any single rule about BSD grep.
    """
    p = tmp_path / "firmware.bin"
    p.write_bytes(b"\xff\xfe\x80\x81" * 20 + NEEDLE.encode())
    return p


def test_whatever_is_called_grep_here_may_miss_a_binary_match(binary_with_secret):
    """The observation this module is built on, kept honest.

    Asserted against `grep` as resolved from PATH — the way a person types it
    — not against a fixed path, because the whole point is that the name does
    not pin the behaviour. If the local grep does find it, the test says so
    and skips rather than pretending: the scanner is right either way, and a
    test that only passes on one machine is worse than none.
    """
    rc = subprocess.run(["grep", "-qF", NEEDLE, str(binary_with_secret)],
                        env={"LC_ALL": "C", "PATH": os.environ.get("PATH", "")}
                        ).returncode
    if rc == 0:
        pytest.skip("the grep on this PATH does find binary matches; "
                    "on the machine this was written on, it did not")
    assert rc != 0


def test_the_scanner_finds_what_grep_misses(binary_with_secret):
    data = scan.read(binary_with_secret)
    assert b"hunter2-not-a-real-password" in data


def test_reading_never_decodes(tmp_path):
    """Decoding is where the trap lives; bytes in, bytes out."""
    p = tmp_path / "x.bin"
    p.write_bytes(b"\xff\xfe\x00invalid utf-8 \x80\x81")
    assert isinstance(scan.read(p), bytes)


def test_an_unreadable_file_is_skipped_not_fatal(tmp_path):
    assert scan.read(tmp_path / "does-not-exist") is None


# --- what counts as a secret ---------------------------------------------

def test_a_private_range_ip_is_not_treated_as_a_secret():
    # It appears throughout the documentation on purpose.
    assert scan.NOT_SECRET.match("192.168.178.105")
    assert scan.NOT_SECRET.match("http://example.local")
    assert not scan.NOT_SECRET.match("Fight-not-the-real-one")


def test_placeholder_values_are_not_scanned_for():
    """Scanning for 'your-network' would match the example header and every
    piece of documentation, and the scanner would be ignored within a day."""
    src = (ROOT / "firmware" / "secrets_local.h.example").read_text()
    for placeholder in ("your-network", "your-password", "paste-the-token"):
        assert placeholder in src


def test_shape_patterns_are_not_applied_to_binaries(tmp_path):
    """mbedTLS carries 'BEGIN ... PRIVATE KEY' as parser strings, so matching
    that in a .bin is meaningless — an early version reported every firmware
    image as leaking a private key."""
    p = tmp_path / "firmware.bin"
    # Assembled, never written out: a literal would make this file a finding.
    p.write_bytes(b"-----BEGIN " + b"RSA " + b"PRIVATE " + b"KEY-----")
    findings = [f for f in scan.scan(include_builds=True) if "firmware.bin" in f[0]]
    assert not any("private key" in what for _, what in findings)


def test_shape_patterns_do_apply_to_text():
    # Assembled at runtime rather than written out: a literal here would make
    # this very file a finding, the same self-match that makes `pgrep -f`
    # match its own command line.
    key = b"-----BEGIN " + b"OPENSSH " + b"PRIVATE " + b"KEY-----"
    assert "private key" in [n for n, rx in scan.PATTERNS if rx.search(key)]
    aws = b"AKIA" + b"0123456789ABCDEF"
    assert any(rx.search(aws) for _, rx in scan.PATTERNS)


# --- what it refuses to flag ---------------------------------------------

def test_the_scanner_does_not_flag_itself():
    """It contains its own search patterns; matching them would be the same
    self-match that makes `pgrep -f` find its own command line."""
    assert pathlib.Path(scan.__file__).resolve() in scan.ALLOWED


def test_the_local_secrets_header_is_never_a_finding():
    assert scan.LOCAL_SECRETS in scan.ALLOWED


def test_the_personal_build_directory_is_excluded():
    """It is meant to hold credentials. Reporting it every run would train
    people to ignore the scanner, which is how a real leak gets through."""
    assert scan.LOCAL_BUILD.name == "cardputer-local"
    for path in scan.iter_files(include_builds=True):
        assert scan.LOCAL_BUILD not in path.parents


# --- the whole thing ------------------------------------------------------

def test_the_repository_is_clean_right_now():
    assert scan.scan(include_builds=True,
                     literals=scan.literals_from_local()) == []


def test_a_planted_secret_would_be_caught():
    """Mutation probe in test form: a scanner that cannot fail is decoration.

    Planted in the *published* build directory, because that is the set the
    scanner deliberately inspects even though .gitignore covers it — release
    assets are copied out of there.
    """
    build = ROOT / "firmware" / ".pio" / "build" / "cardputer"
    if not build.exists():
        pytest.skip("no build output to plant into")
    planted = build / "planted_secret_probe.bin"
    planted.write_bytes(b"\x00" * 32 + b"totally-secret-value" + b"\x00" * 32)
    try:
        findings = scan.scan(include_builds=True,
                             literals=["totally-secret-value"])
        assert any("planted_secret_probe" in path for path, _ in findings)
    finally:
        planted.unlink()


def test_gitignored_files_outside_the_build_are_skipped():
    """They are never published, so flagging them is noise."""
    for path in scan.iter_files(include_builds=True):
        if ".pio" not in path.parts:
            assert not scan.is_ignored(path), path
