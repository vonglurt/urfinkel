# Contributing to UR FINKEL

**Fork it.** That is the encouraged path, and it needs no permission from
anyone. Take the renderer, take the ruleset, take the music engine, port it
to a C16 or a C64 or something that has never run a game of Ur — the licence
below allows all of it. A fork that goes somewhere this project will not is
worth more than a pull request that waters it down.

If you want a change to land *here*, read on. There is one rule that is not
negotiable and a handful of constraints that will bite you if nobody warns
you first.

## Two directories are not hand-edited

`build/` and `vendor/` are both generated, and a commit that edits either by
hand will be rejected by the push gate rather than merged.

- **`build/`** — the binaries, their checksum sidecars, `CHECKSUMS.txt` and the
  two collections. `make checksums` produces every number in them, and the
  same script writes the generated blocks in `README.md`, `INSTALL.md` and
  `docs/index.html`. Change a source file, not a published figure.
- **`vendor/emulatorjs/`** — **somebody else's code, under a different
  licence.** EmulatorJS is GPL-3.0 and the Plus/4 core inside it is VICE,
  GPL-2.0-or-later; this project is MIT. It is copied in verbatim so the
  offline collection needs no network, and `vendor/emulatorjs/SOURCE.txt`
  records the version and where the corresponding source lives. To move to a
  newer EmulatorJS, edit the version in `tools/vendor-emulator.sh` and re-run
  it; `tools/vendor-emulator.sh --check` verifies what is there against the
  manifest it wrote.

## Every commit must be signed

**Unsigned commits will not be merged.** Not as a matter of taste — this is a
project whose whole claim is that its numbers were measured rather than
assumed, and an unsigned commit is an unattributable one. If the history is
going to be evidence, every line of it has to say who wrote it and prove it.

Any signature GitHub will verify is acceptable. SSH is the least trouble if
you already have a key:

```sh
git config --global gpg.format ssh
git config --global user.signingkey ~/.ssh/id_ed25519.pub
git config --global commit.gpgsign true
git config --global tag.gpgsign true
```

Then register the key at **github.com/settings/keys**. The step everyone
misses: a key added as an *Authentication Key* does **not** verify
signatures. Add the same public key a second time with **Key type: Signing
Key**. You will see the identical key listed twice; that is correct.

Check your work before you push:

```sh
git log --format='%G? %GS %an <%ae> %s' -3
```

`G` in the first column is a good signature. `N` means unsigned and will be
rejected. If you see `error: cannot run gpg`, that is git falling back to a
GPG binary you do not have — it means `gpg.format ssh` did not take.

To verify signatures locally rather than only on GitHub, point git at an
allowed-signers file:

```sh
git config --global gpg.ssh.allowedSignersFile ~/.ssh/allowed_signers
```

containing one line per signer: `you@example.com namespaces="git" ssh-ed25519 AAAA…`

If you have already made unsigned commits on a branch, sign them in place
rather than piling a fixup on top:

```sh
git rebase --exec 'git commit --amend --no-edit -S' origin/main
```

## The gate

```sh
make check && make conform
```

Both must pass before a pull request is opened. `make check` builds the
ruleset and keyboard tests with the **host** compiler and runs them — the
rules are deliberately free of the machine so they can be tested without an
emulator. `make conform` renders the opening board and compares it, byte for
byte, against `test/golden-board.png`.

## The pre-commit hook

`build/urfinkel.prg` and `build/urfinkel.d64` are tracked, because the README
offers them as downloads and the play page loads one of them. A stale binary
is therefore a *visitor's* problem, not a developer's, so a hook rebuilds them
from nothing before every commit:

```sh
make hooks      # or, the same thing by hand:
git config core.hooksPath tools/hooks
```

It runs `make clean && make && make disk`, runs `make check`, and stages the
result — about a second and a half. A failing test refuses the commit.
`URFINKEL_CONFORM=1 git commit …` adds `make conform`, which is left out by
default only because it drives an emulator. `git commit --no-verify` skips the
lot, which is reasonable for a documentation change and nothing else.

It **rebuilds and stages** rather than checking that the committed binary
already matched, and that is deliberate: the Makefile stamps the program with
`date +%Y-%m-%d` and draws it on the menu, so the same sources built on two
different days are byte-different on purpose. A comparison would fail every
morning for a reason that is not a defect.

There is a `pre-push` hook as well, and it *does* compare — exactly. It reads
the build stamp back out of the binary being pushed, rebuilds clean at that
same stamp, and requires every byte of both the `.prg` and the `.d64` to
match. That closes the gap the first hook leaves: a commit made with
`--no-verify`, an amend, a rebase or a merge cannot push a binary these
sources do not produce. It takes about two seconds.

## Two constraints that will surprise you

**The machine is full, and the link fails rather than growing.** Song data
is capped by `MIDBUDGET` in the Makefile, currently 22 200 bytes, and that
ceiling is the program's shock absorber: it goes *down* as the code goes up,
so the cost of a feature is paid in music and is visible in one place
instead of appearing as an unexplained link failure. The last measurement
left 364 bytes free. If your change does not link, you have not found a bug
— you have found the budget. Run `make music-budget` to see what the last
link actually left, and lower `MIDBUDGET` deliberately, in a commit that
says which songs it cost.

**Re-baselining the board is a judgement, not a formality.** If `make
conform` fails, *look at the two images* before doing anything else. The
golden board is the approved rendering, and `make conform-bless` is a
separate command you have to type on purpose precisely so that it cannot
happen by reflex. A conformance failure you did not intend is the test doing
its job.

## Building

```sh
brew install cc65 vice     # the cross compiler, and xplus4 to run it
make                       # build/urfinkel.prg
make disk                  # build/urfinkel.d64
make run                   # boot it in xplus4
```

`build/urfinkel.prg` and `build/urfinkel.d64` are tracked on purpose, so the
download links in the README resolve. If your change alters either, commit
the rebuilt binary with it.

Note that `assets/midi/` is not published here; `src/song.h` ships generated,
so the game builds without it, but the transcribed songs cannot be
regenerated from a clean checkout. `tools/bed-order.txt` sets the order the
beds play in and is honoured whenever the assets *are* present — but with
them absent, reordering means editing the committed `tools/songs-midi.mml`
and `src/song_beds.h` by hand to match it, and rebuilding with `make music`.

## Style

Match the file you are editing. The comments in this codebase explain *why* a
thing is the way it is — which trap was hit, which measurement forced the
decision — and that is the convention worth keeping. A comment restating what
the line already says is noise; a comment recording that an IRQ handler reads
its own variables out of ROM unless the linker is told otherwise saves the
next person a day.

## Licence

MIT — see [LICENSE](LICENSE). By contributing you agree that your work is
licensed under the same terms, and that you have the right to license it.

## Contributors

- **Paul Richeson** ([vonglurt](https://github.com/vonglurt)) — the concept,
  the basic prototype, the port, the reports, and the restored Plus/4 it
  was measured on
