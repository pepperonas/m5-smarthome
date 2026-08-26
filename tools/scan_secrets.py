#!/usr/bin/env python3
"""Scan the working tree and built binaries for secrets before publishing.

Exists because the obvious one-liner cannot be trusted:

    grep -qF "$PASSWORD" firmware.bin && echo LEAK

That printed nothing here while the password sat in the file — a scan whose
whole purpose was checking a public artefact reported it clean.

The cause is not the one it first looked like. `/usr/bin/grep` (BSD grep)
handles this correctly. The `grep` that was actually running was **ugrep**,
installed ahead of it on PATH, which stays quiet on a binary match. Which
implementation answers depends on the shell, the PATH and the machine — and a
check whose answer depends on that is not a check.

So this reads the bytes itself. No subprocess, no decoding, no locale.

    python3 tools/scan_secrets.py                 patterns only
    python3 tools/scan_secrets.py --with-local    also the real values from
                                                  firmware/secrets_local.h
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LOCAL_SECRETS = ROOT / "firmware" / "secrets_local.h"

SKIP_DIRS = {".git", "__pycache__", "node_modules", ".pytest_cache", ".venv"}

#: Expected to hold real credentials, and gitignored. Never a finding.
ALLOWED = {LOCAL_SECRETS, pathlib.Path(__file__).resolve()}

#: The build directory of the personal environment. It is *supposed* to
#: contain credentials; the point is that nothing from it is ever published.
LOCAL_BUILD = ROOT / "firmware" / ".pio" / "build" / "cardputer-local"

#: Shapes worth refusing regardless of value. Applied to **text files only**:
#: a firmware binary links mbedTLS, whose PEM parser carries these very
#: strings as literals, so matching them in a .bin means nothing at all.
PATTERNS = [
    ("private key", re.compile(rb"BEGIN (?:RSA |OPENSSH |EC |DSA )?PRIVATE KEY")),
    ("AWS access key", re.compile(rb"AKIA[0-9A-Z]{16}")),
    ("OpenAI-style key", re.compile(rb"sk-[A-Za-z0-9]{32,}")),
]

#: Values that look secret but are not, so a scan does not cry wolf.
NOT_SECRET = re.compile(r"^(?:\d{1,3}\.){3}\d{1,3}$|^https?://")


def is_ignored(path):
    """Gitignored files are never published, so they are not findings."""
    import subprocess
    return subprocess.run(["git", "check-ignore", "-q", str(path)],
                          cwd=ROOT, capture_output=True).returncode == 0


def iter_files(include_builds: bool):
    """Everything that could plausibly be published.

    The personal build directory is excluded by name: it is meant to contain
    credentials, and treating that as a finding would train people to ignore
    the scanner — which is how a real one gets missed.
    """
    for path in ROOT.rglob("*"):
        if not path.is_file() or path.resolve() in ALLOWED:
            continue
        parts = set(path.parts)
        if parts & SKIP_DIRS:
            continue
        if LOCAL_BUILD in path.parents:
            continue
        if ".pio" in parts:
            # Inside .pio only what we would ever hand out.
            if not include_builds or path.suffix != ".bin":
                continue
        elif is_ignored(path):
            continue
        yield path


def read(path):
    """Bytes, or None for something unreadable. Never decodes — a secret in a
    binary is still a secret, and decoding is where the grep trap lives."""
    try:
        return path.read_bytes()
    except OSError:
        return None


def literals_from_local():
    """The concrete values of this installation, from the gitignored header.

    Scanning for patterns alone would miss a password that happens to look
    like an ordinary word.
    """
    if not LOCAL_SECRETS.exists():
        return []
    out = []
    for m in re.finditer(r'#define\s+\w+\s+"([^"]{4,})"',
                         LOCAL_SECRETS.read_text()):
        value = m.group(1)
        if not value or value.startswith("your-") or value == "paste-the-token":
            continue
        if NOT_SECRET.match(value):
            continue          # a private-range IP is documentation, not a secret
        out.append(value)
    return out


def scan(include_builds=True, literals=()):
    findings = []
    encoded = [(lit, lit.encode()) for lit in literals]
    for path in iter_files(include_builds):
        data = read(path)
        if data is None:
            continue
        rel = path.relative_to(ROOT)
        # Shape patterns only make sense in text. In a binary they are almost
        # always a library's own parser strings.
        if path.suffix != ".bin":
            for name, rx in PATTERNS:
                if rx.search(data):
                    findings.append((str(rel), name))
        for lit, raw in encoded:
            if raw in data:
                findings.append((str(rel), f"literal secret ({lit[:4]}…)"))
    return findings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-local", action="store_true",
                    help="also search for the concrete values in secrets_local.h")
    ap.add_argument("--no-builds", action="store_true",
                    help="skip .pio build output")
    args = ap.parse_args()

    literals = literals_from_local() if args.with_local else []
    if args.with_local and not literals:
        print("note: no local secrets configured, scanning patterns only")

    findings = scan(include_builds=not args.no_builds, literals=literals)
    if LOCAL_BUILD.exists() and literals:
        print(f"note: {LOCAL_BUILD.relative_to(ROOT)} holds a personal build "
              f"with credentials compiled in — never publish anything from it")
    if findings:
        print("SECRETS FOUND — do not publish:")
        for path, what in findings:
            print(f"  {path}: {what}")
        return 1
    scope = "tree" + ("" if args.no_builds else " and build output")
    print(f"clean: no secrets in the {scope}"
          + (f" ({len(literals)} literal(s) checked)" if literals else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
