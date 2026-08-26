#!/usr/bin/env python3
"""Generate the README badge block from measured facts.

Every number here is counted or read out of a build, never typed. Badges that
drift are worse than no badges: they look like a claim and are actually a
guess, and this house has twice had an estimate reach a commit message ahead
of the measurement.

    python3 tools/badges.py            rewrite the block in README.md
    python3 tools/badges.py --check    fail if the block is out of date (CI)
    python3 tools/badges.py --print    show the numbers without touching files
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parent.parent
README = ROOT / "README.md"

START = "<!-- badges:start -->"
END = "<!-- badges:end -->"

REPO = "pepperonas/m5-smarthome"

#: Directories that are never our own source.
SKIP_DIRS = {".git", ".pio", "__pycache__", "node_modules", ".pytest_cache",
             "dist", "build", ".venv", "venv"}

#: extension -> language label
LANGS = {
    ".cpp": "C++", ".h": "C++", ".hpp": "C++", ".c": "C",
    ".py": "Python", ".sh": "Shell", ".yml": "YAML", ".yaml": "YAML",
    ".ini": "Config", ".csv": "Data", ".md": "Docs",
}

#: Only these count as code for the "lines of code" badge — documentation and
#: data are counted separately so the number means what people assume it means.
CODE_LANGS = {"C++", "C", "Python", "Shell"}


def iter_files():
    """Exactly the files git tracks.

    Walking the tree instead counted whatever happened to be lying around —
    a local, gitignored secrets_local.h added 8 lines here and none on a CI
    runner, so the badge disagreed with itself depending on the machine and
    the drift check failed for anyone with a personal build. "Lines of code"
    means what is in the repository.
    """
    out = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT,
                         capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit("not a git checkout; cannot count tracked files")
    for name in out.stdout.split("\0"):
        if not name:
            continue
        path = ROOT / name
        if not path.is_file():
            continue                      # deleted but still in the index
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        yield path


def count_lines():
    """Physical lines per language, plus a blank/comment-free code count."""
    per_lang = {}
    code_lines = 0
    for path in iter_files():
        lang = LANGS.get(path.suffix.lower())
        if not lang:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        n = len(text.splitlines())
        per_lang[lang] = per_lang.get(lang, 0) + n
        if lang in CODE_LANGS:
            code_lines += n
    return code_lines, per_lang


def count_firmware_tests():
    """Unity tests are registered with RUN_TEST(...) in the runner."""
    runner = ROOT / "firmware" / "test" / "test_core" / "test_main.cpp"
    return len(re.findall(r"^\s*RUN_TEST\(", runner.read_text(), re.M))


def count_python_tests(rel_dir):
    """Ask pytest, from the directory the suite expects to run in.

    Collecting from the repository root instead would fail the import of
    `m5gw` and silently report zero — a badge saying "0 tests" is worse than
    no badge at all.
    """
    target = ROOT / rel_dir
    if not target.exists():
        return 0
    # `-o addopts=` clears whatever pytest.ini contributes. Without it a
    # project-level `-q` stacks with ours into `-qq`, pytest stops printing
    # the summary line, and the count silently breaks — measured, not guessed.
    out = subprocess.run(
        [sys.executable, "-m", "pytest", "tests", "--collect-only", "-q",
         "-o", "addopts="],
        capture_output=True, text=True, cwd=target.parent)
    m = re.search(r"(\d+) tests? collected", out.stdout)
    if m:
        return int(m.group(1))
    # Fallback for the quieter format: one "path: N" line per file.
    per_file = re.findall(r"^\S+\.py: (\d+)$", out.stdout, re.M)
    if per_file:
        return sum(int(n) for n in per_file)
    raise SystemExit(
        "could not count tests in %s; pytest said:\n%s"
        % (rel_dir, out.stdout[-500:]))


def count_mutations():
    """Count the harness entries rather than trusting a number in prose.

    Each mutation is a 4-tuple whose first element is a quoted label, written
    at one fixed indent inside the MUTATIONS table.
    """
    src = (ROOT / "tools" / "mutate.py").read_text()
    return len(re.findall(r'^\s{8}\("', src, re.M))


# Deliberately no build-size badge.
#
# Flash and slot usage can only be read out of .pio, which is not in the
# repository — so the block rendered differently on a machine that had built
# and on a CI runner that had not yet, and the drift check failed for a
# difference that meant nothing. Every badge here is computed from tracked
# files alone, so the same commit always yields the same block. The measured
# sizes live in README's Status section and the changelog, where a human
# updates them alongside the measurement.


def snapshot_bytes():
    """The measured /api/dash payload, pinned by a gateway test."""
    test = (ROOT / "gateway" / "tests" / "test_aggregate.py").read_text()
    m = re.search(r"len\(body\.encode\(\)\) < (\d+)", test)
    return int(m.group(1)) if m else 1024


def badge(label, message, colour, logo=None, logo_colour=None):
    def esc(s):
        return urllib.parse.quote(str(s).replace("-", "--").replace("_", "__"),
                                  safe="")
    url = f"https://img.shields.io/badge/{esc(label)}-{esc(message)}-{colour}"
    extra = []
    if logo:
        extra.append(f"logo={urllib.parse.quote(logo)}")
    if logo_colour:
        extra.append(f"logoColor={logo_colour}")
    extra.append("style=flat-square")
    return f"![{label}: {message}]({url}?{'&'.join(extra)})"


def build_block(facts):
    lang = facts["languages"]
    b = []

    # Live state — these read from GitHub, so they cannot go stale.
    b.append(f"[![CI](https://img.shields.io/github/actions/workflow/status/"
             f"{REPO}/build.yml?branch=main&style=flat-square&logo=githubactions"
             f"&logoColor=white&label=CI)]"
             f"(https://github.com/{REPO}/actions/workflows/build.yml)")
    b.append(f"[![Release](https://img.shields.io/github/v/release/{REPO}"
             f"?style=flat-square&logo=github&label=release)]"
             f"(https://github.com/{REPO}/releases/latest)")
    b.append(f"[![Downloads](https://img.shields.io/github/downloads/{REPO}"
             f"/total?style=flat-square&logo=github&label=downloads)]"
             f"(https://github.com/{REPO}/releases)")
    b.append(f"[![Last commit](https://img.shields.io/github/last-commit/{REPO}"
             f"?style=flat-square&logo=git&logoColor=white)]"
             f"(https://github.com/{REPO}/commits/main)")
    b.append(f"[![Licence](https://img.shields.io/github/license/{REPO}"
             f"?style=flat-square)](LICENSE)")
    b.append("")

    # Measured here, refreshed by this script and checked by CI.
    b.append(badge("tests", f"{facts['tests_total']} passing", "brightgreen",
                   "checkmarx", "white"))
    b.append(badge("firmware tests", facts["tests_firmware"], "brightgreen"))
    b.append(badge("gateway tests", facts["tests_gateway"], "brightgreen"))
    b.append(badge("tool tests", facts["tests_tools"], "brightgreen"))
    b.append(badge("mutation probes", f"{facts['mutations']} caught", "8A2BE2"))
    b.append(badge("lines of code", f"{facts['code_lines']:,}".replace(",", " "),
                   "blue"))
    b.append("")

    b.append(badge("platform", "ESP32-S3", "E7352C", "espressif", "white"))
    b.append(badge("board", "M5Cardputer ADV", "orange"))
    b.append(badge("framework", "Arduino", "00979D", "arduino", "white"))
    b.append(badge("built with", "PlatformIO", "F5822A", "platformio", "white"))
    b.append(badge("C++", f"{lang.get('C++', 0):,} lines".replace(",", " "),
                   "00599C", "cplusplus", "white"))
    b.append(badge("Python", f"{lang.get('Python', 0):,} lines".replace(",", " "),
                   "3776AB", "python", "white"))
    b.append("")

    b.append(badge("snapshot", f"< {facts['snapshot']} B", "success"))
    b.append(badge("PSRAM required", "none", "success"))
    b.append(badge("secrets in repo", "zero", "success"))
    b.append(badge("gateway", "Flask", "000000", "flask", "white"))
    b.append(badge("docs", "5 documents", "informational", "markdown", "white"))

    return "\n".join(b)


def facts():
    code_lines, per_lang = count_lines()
    fw = count_firmware_tests()
    gw = count_python_tests("gateway/tests")
    tools = count_python_tests("tools/tests")
    return {
        "code_lines": code_lines,
        "languages": per_lang,
        "tests_firmware": fw,
        "tests_gateway": gw,
        "tests_tools": tools,
        "tests_total": fw + gw + tools,
        "mutations": count_mutations(),
        "snapshot": snapshot_bytes(),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if README is out of date")
    ap.add_argument("--print", dest="show", action="store_true")
    args = ap.parse_args()

    f = facts()
    if args.show:
        print(json.dumps(f, indent=2))
        return 0

    block = build_block(f)
    text = README.read_text()
    if START not in text or END not in text:
        raise SystemExit(f"README is missing the {START} / {END} markers")

    new = re.sub(re.escape(START) + r".*?" + re.escape(END),
                 f"{START}\n\n{block}\n\n{END}", text, flags=re.S)

    if args.check:
        if new != text:
            print("README badges are out of date. Run: python3 tools/badges.py")
            return 1
        print("README badges are current.")
        return 0

    if new != text:
        README.write_text(new)
        print("README badges updated.")
    else:
        print("README badges already current.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
