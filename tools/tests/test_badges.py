"""The badge generator.

A badge is a claim. If it drifts it becomes a confident lie, so the counting
is pinned here and CI checks the rendered block for staleness — the same
treatment the generated IR table gets, and for the same reason.
"""

import importlib.util
import pathlib
import re
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("badges", ROOT / "tools" / "badges.py")
badges = importlib.util.module_from_spec(spec)
sys.modules["badges"] = badges
spec.loader.exec_module(badges)


# --- counting -------------------------------------------------------------

def test_firmware_tests_are_counted_from_the_runner():
    n = badges.count_firmware_tests()
    assert n > 0
    # Every RUN_TEST must name a function that exists, or the count is fiction.
    runner = (ROOT / "firmware" / "test" / "test_core" / "test_main.cpp").read_text()
    names = re.findall(r"RUN_TEST\((\w+)\)", runner)
    assert len(names) == n
    assert len(set(names)) == n, "a test is registered twice"


def test_registered_tests_are_actually_defined():
    """A typo in RUN_TEST would not compile, but a stale registration would
    still inflate the badge.

    Reads every test file in the directory rather than a hard-coded list: the
    first version named two files and went red the moment a third was added,
    which is a pin failing for its own bookkeeping instead of for a defect.
    """
    test_dir = ROOT / "firmware" / "test" / "test_core"
    sources = sorted(test_dir.glob("*.cpp"))
    assert len(sources) >= 2, "test sources not found"
    combined = "".join(p.read_text() for p in sources)
    defined = set(re.findall(r"^void (test_\w+)\(void\)\s*\{", combined, re.M))
    runner = (test_dir / "test_main.cpp").read_text()
    for name in re.findall(r"RUN_TEST\((\w+)\)", runner):
        assert name in defined, f"{name} is registered but never defined"


def test_mutations_are_counted_from_the_harness():
    src = (ROOT / "tools" / "mutate.py").read_text()
    n = badges.count_mutations()
    assert n == len(re.findall(r'^\s{8}\("', src, re.M))
    assert n >= 10, "the harness lost entries"


def test_python_tests_are_collected_from_the_right_directory():
    """Collecting from the repo root fails to import m5gw and silently
    reports zero. A badge reading '0 tests' is worse than none."""
    assert badges.count_python_tests("gateway/tests") > 0
    assert badges.count_python_tests("tools/tests") > 0


def test_counting_survives_a_project_level_addopts():
    """pytest.ini contributes `-q`; stacked with ours that becomes `-qq`,
    pytest stops printing the summary line, and the count breaks. Found by
    this suite going red the moment a pytest.ini was added."""
    import subprocess
    out = subprocess.run(
        [sys.executable, "-m", "pytest", "tests", "--collect-only", "-q"],
        capture_output=True, text=True, cwd=ROOT / "gateway")
    assert "tests collected" not in out.stdout, (
        "the fragile format came back; the fallback below is now untested")
    assert badges.count_python_tests("gateway/tests") > 0


def test_a_missing_suite_counts_as_zero_not_an_error():
    assert badges.count_python_tests("does/not/exist") == 0


def test_line_counting_excludes_build_output():
    code, per_lang = badges.count_lines()
    assert code > 1000
    assert per_lang["C++"] > 0 and per_lang["Python"] > 0
    # .pio holds megabytes of vendored library source; counting it would turn
    # "lines of code" into "lines of somebody else's code".
    assert code < 20000, "build or dependency output is being counted"


def test_gitignored_files_are_not_counted():
    """A gitignored secrets_local.h added 8 lines on the author's machine and
    none on a CI runner, so the badge disagreed with itself and the drift
    check failed for anyone with a personal build.

    Note this is about *ignored*, not merely unstaged: a source file written
    and not yet added still belongs to the commit and does count — see
    test_staging_a_file_does_not_change_the_count.
    """
    probe = ROOT / "firmware" / "secrets_ignored_probe.h"   # matches secrets*.h
    assert badges.subprocess.run(
        ["git", "check-ignore", "-q", str(probe)], cwd=ROOT).returncode == 0, \
        "the probe path is not actually ignored; the test proves nothing"
    before, _ = badges.count_lines()
    probe.write_text("\n" * 50)
    try:
        after, _ = badges.count_lines()
    finally:
        probe.unlink()
    assert after == before, "an ignored file changed the line count"


def test_staging_a_file_does_not_change_the_count():
    """The count must describe the commit, not the index.

    A newly written file was invisible before `git add` and counted after, so
    badges generated during development disagreed with the same commit checked
    out in CI. Twice.
    """
    probe = ROOT / "firmware" / "staging_probe.h"
    probe.write_text("// probe\n" * 10)
    try:
        unstaged, _ = badges.count_lines()
        subprocess.run(["git", "add", str(probe)], cwd=ROOT, check=True,
                       capture_output=True)
        staged, _ = badges.count_lines()
    finally:
        subprocess.run(["git", "rm", "-f", "--quiet", "--ignore-unmatch",
                        str(probe)], cwd=ROOT, capture_output=True)
        probe.unlink(missing_ok=True)
    assert unstaged == staged, "the line count depends on the index"


def test_documentation_is_not_counted_as_code():
    code, per_lang = badges.count_lines()
    assert per_lang.get("Docs", 0) > 0
    assert code == sum(v for k, v in per_lang.items() if k in badges.CODE_LANGS)
    assert "Docs" not in badges.CODE_LANGS


# --- rendering ------------------------------------------------------------

def test_badge_escapes_characters_shields_treats_specially():
    # A literal dash or underscore in a label would be eaten by shields.io.
    out = badges.badge("board", "M5Cardputer ADV", "orange")
    assert "M5Cardputer%20ADV" in out
    assert "ESP32--S3" in badges.badge("platform", "ESP32-S3", "red")
    assert "a__b" in badges.badge("x", "a_b", "red")


def test_the_block_reports_the_measured_totals():
    f = badges.facts()
    block = badges.build_block(f)
    assert f"tests-{f['tests_total']}%20passing" in block
    assert f"{f['mutations']}%20caught" in block
    assert "m5-smarthome" in block          # links point at this repo


def test_every_badge_line_is_a_markdown_image():
    block = badges.build_block(badges.facts())
    for line in filter(None, block.splitlines()):
        assert line.startswith("![") or line.startswith("[!["), line


def test_the_block_does_not_depend_on_build_output():
    """The same commit must render the same badges everywhere.

    An earlier version read flash usage out of .pio, which exists on a machine
    that has built and not on a CI runner that has not — so the drift check
    failed over a difference that meant nothing, twice.
    """
    # Assert on the mechanism, not on words: an earlier version of this test
    # searched for "RAM" and matched the "PSRAM required" badge.
    assert "build" not in badges.facts(), "a build-derived value is back"

    import tempfile
    stash = ROOT / "firmware" / ".pio"
    if not stash.exists():
        pytest.skip("no build output to hide")
    # Must leave the tree entirely: renaming it inside the repo would make it
    # stop matching the .pio/ ignore rule, so its thousands of vendored files
    # would suddenly be counted — which is a different failure wearing the
    # same red.
    moved = pathlib.Path(tempfile.mkdtemp()) / "pio"
    with_build = badges.build_block(badges.facts())
    stash.rename(moved)
    try:
        without_build = badges.build_block(badges.facts())
    finally:
        moved.rename(stash)
    assert with_build == without_build


def test_readme_carries_the_markers():
    text = badges.README.read_text()
    assert badges.START in text and badges.END in text
    assert text.index(badges.START) < text.index(badges.END)


def test_the_committed_readme_is_up_to_date():
    """The drift check CI runs. Same idea as the IR table: a stale artefact
    that looks authoritative must not survive a push."""
    text = badges.README.read_text()
    expected = re.sub(
        re.escape(badges.START) + r".*?" + re.escape(badges.END),
        f"{badges.START}\n\n{badges.build_block(badges.facts())}\n\n{badges.END}",
        text, flags=re.S)
    assert text == expected, "run: python3 tools/badges.py"


def test_check_mode_passes_on_a_current_readme():
    sys.argv = ["badges.py", "--check"]
    assert badges.main() == 0


def test_the_module_badge_counts_what_the_documentation_tables_count():
    """Two places decide what a "pure module" is: this badge, and the guard
    that demands a documentation row for each. They must agree, or the README
    claims a number the docs contradict — which is what happened when the
    first generated data table landed and the badge counted it as a module.
    """
    import test_docs_sync
    assert badges.count_pure_modules() == len(test_docs_sync._core_modules())


def test_a_generated_data_table_is_not_counted_as_a_module():
    """The property behind the number, so the count cannot drift back by
    someone reintroducing a plain glob."""
    core = ROOT / "firmware" / "lib" / "core"
    generated = [p for p in core.glob("*.h")
                 if p.read_text(errors="ignore").lstrip().startswith("// GENERATED")]
    assert generated, "no generated header on disk — this pin has nothing to prove"
    assert badges.count_pure_modules() == len(list(core.glob("*.h"))) - len(generated)
