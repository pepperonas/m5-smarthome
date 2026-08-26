# Contributing

## Before you push

```bash
./tools/preflight.sh
```

It runs everything CI runs, in the order that makes it pass: badges are
regenerated **before** the drift check, because adding a test changes the
test-count badge. Run the steps by hand in the wrong order and the failure
looks spurious, which is how a red tree gets pushed. That happened once.

## How this codebase is arranged

Everything deterministic is **pure** and lives where it can be tested on a
host: `firmware/lib/core` and the pure halves of `gateway/m5gw`. They take
data and return data — no sockets, no display, not even a clock, since callers
pass `now`.

Everything that touches hardware is a **thin adapter** on top. Those cannot be
unit-tested, so their invariants are pinned against their own source by
`tools/tests/test_firmware_shell.py`.

That second half is not architectural elegance, it is scar tissue: twice, the
worst defect in this project sat in code declared too thin to test. If you add
to the shell, add a pin for it.

## Writing tests

Read [`docs/TESTING.md`](docs/TESTING.md). The short version:

- **A new test that fails is a suspected defect.** Investigate before you
  touch the expectation. Several real bugs here were found exactly that way.
- **If the test turns out to be wrong, fix it to assert the intent**, not to
  rubber-stamp what the code happens to do — and write down why.
- **Mutate every new pin once.** Break the thing it guards and watch it go
  red. A test you have never seen fail is not an assurance; several here were
  green on arrival and proved nothing.
- **Assert on the mechanism, not on a word.** Word searches match comments,
  neighbouring features, and their own source file.
- **Text checks run against comment-free source.** A comment explaining a
  removed defect contains the very words the check looks for.

## Adding a feature

1. Put the decision in `lib/core` or a pure gateway module, with tests.
2. Add its line to `features.txt` **in the same commit** — house rule.
3. If it adds a key, add it to the README key table. A test enforces this.
4. If it adds a gateway action, give it a row in [`docs/API.md`](docs/API.md).
   A test enforces this too.
5. Add a mutation to `tools/mutate.py` if it is load-bearing.

## Secrets

Never commit one. Not in code, not in a test fixture, not in a binary.

```bash
python3 tools/scan_secrets.py --with-local
```

It reads bytes rather than shelling out to `grep`, because `grep -qF
"$SECRET" firmware.bin` printed nothing here while the secret was in the file.
See [`docs/PITFALLS.md`](docs/PITFALLS.md).

`firmware/secrets_local.h` is gitignored and is for personal builds only. The
published `cardputer` environment cannot read it; that separation is the point
and should stay.

## Commit messages

Explain **why**, in English. The interesting part of a change is usually the
reasoning that is not visible in the diff: what was measured, what was tried
and rejected, which assumption turned out to be wrong. Several messages here
correct an earlier diagnosis of mine, and that is the correct use of them.

## Hardware

Most of this was written without a Cardputer attached, and it shows in the
defect history. If you have one, [`docs/MEASURE.md`](docs/MEASURE.md) lists
what has never been measured — starting with whether the keyboard controller
really wakes the ESP32 from deep sleep, which the whole power design rests on.
