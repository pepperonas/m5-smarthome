"""Tests for the mutation harness itself.

The harness edits source files in place, so a run that dies before its
cleanup leaves the tree silently broken. That happened: a killed run left the
"rollback is a no-op" mutation in `optimistic.cpp`, and the next suite run
reported a failing guarantee that was never actually broken by any commit.

A `finally` does not survive SIGKILL. These pin the crash-recovery journal
that does.
"""

import json
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import mutate  # noqa: E402


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """A throwaway working tree with one source file and one journal."""
    src = tmp_path / "src.cpp"
    src.write_text("int f() { return ORIGINAL; }\n")
    monkeypatch.setattr(mutate, "JOURNAL", tmp_path / ".mutate-journal.json")
    return tmp_path, src


def test_the_journal_is_written_before_the_file_is_touched(tree):
    """Ordering is the whole point: if the journal were written after the
    mutation, a kill in between would leave a mutated file nobody can find."""
    tmp, src = tree
    order = []
    real_write = pathlib.Path.write_text

    def spy(self, text, *a, **kw):
        order.append(self.name)
        return real_write(self, text, *a, **kw)

    original = src.read_text()
    with pytest.MonkeyPatch.context() as mp:
        mp.setattr(pathlib.Path, "write_text", spy)
        mutate.begin_mutation(src, original)
    assert order == [mutate.JOURNAL.name], (
        "begin_mutation must record the original before anything writes to "
        "the source file")


def test_a_killed_run_is_recovered_on_the_next_start(tree):
    """Simulates SIGKILL: the journal exists, the file is mutated, no
    finally ever ran."""
    tmp, src = tree
    original = src.read_text()
    mutate.begin_mutation(src, original)
    src.write_text("int f() { return MUTATED; }\n")

    recovered = mutate.recover()

    assert recovered == [src]
    assert src.read_text() == original, "the leftover mutation was not undone"
    assert not mutate.JOURNAL.exists(), "the journal outlived its purpose"


def test_recovery_is_a_no_op_on_a_clean_tree(tree):
    tmp, src = tree
    before = src.read_text()
    assert mutate.recover() == []
    assert src.read_text() == before


def test_finishing_a_mutation_clears_the_journal(tree):
    tmp, src = tree
    original = src.read_text()
    mutate.begin_mutation(src, original)
    src.write_text("mutated")
    mutate.end_mutation(src)
    assert src.read_text() == original
    assert not mutate.JOURNAL.exists()


def test_recovery_survives_a_corrupt_journal(tree):
    """A half-written journal must not become a second failure mode: it can
    no longer restore anything, so it must say so rather than crash the run
    or silently pretend the tree is clean."""
    tmp, src = tree
    mutate.JOURNAL.write_text("{not json")
    with pytest.raises(SystemExit) as e:
        mutate.recover()
    assert "journal" in str(e.value).lower()


def test_the_journal_is_not_committable():
    """It holds source text mid-edit; it has no business in a commit."""
    ignored = (ROOT / ".gitignore").read_text()
    assert ".mutate-journal.json" in ignored
