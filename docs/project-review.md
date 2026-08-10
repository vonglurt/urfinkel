# UR FINKEL — project review

What the migration from interpreted BASIC 3.5 to compiled 6502 actually
delivered, what it cost, and what is still wrong with it.

The predecessor is frozen and unpublished here, but it is not a draft — it
is a complete, correct, playable game and the oracle this edition is judged
against. Nothing below is a criticism of it.
Every number in the "BASIC" column is the price of an interpreter, not of
a design.

---

## 1. The headline

| | BASIC 3.5 | Compiled | |
|---|---:|---:|---|
| draw the board | 23 960 ms | 123.4 ms | **194×** |
| paint one 4×4 square | 594 ms | 2.05 ms | **290×** |
| refresh both token rows | 1 826 ms | 7.48 ms | **244×** |
| find the legal moves | 2 785 ms | 4.83 ms | **577×** |
| binary size | 25 681 B | 25 137 B | 0.98× |

A turn's rendering and rule work goes from about ten seconds to about
forty milliseconds. That is the whole project in one line, and it is worth
being precise about what it bought, because the answer is not "the same
game, faster".

## 2. What the speed was actually spent on

Almost none of it was banked. It was spent on things the frozen edition's
own documentation lists as out of scope **on performance grounds** — which
is the only honest test of whether a migration was worth doing.

- **Background music during play.** The BASIC edition documents this as
  impossible under its constraint: steady playback "needs a machine
  language IRQ player (`SYS`), which is out of scope for the pure-BASIC
  constraint". There is now a raster interrupt at 200 Hz and a cooperative
  sequencer at 50 Hz, and the bed keeps breathing while the game sits
  waiting for a human to choose a piece.
- **Motion.** A piece used to move one whole board square per step,
  because a square is 32 `POKE`s and a `POKE` was 23 ms. It now travels
  one character cell at a time — five steps where there was one.
- **A tumble that reads as dice.** One to three frames became sixteen to
  twenty-one.
- **The cabinet.** A theatre front, three rails of chasing lamps, and a
  curtain that travels fourteen columns instead of six.
- **A trace facility**, which an interpreter could not have carried at
  all: a scrolling program log costs 288 screen bytes a line, and at 23 ms
  a `POKE` that is six seconds.

## 3. Conformance, which is the part that makes the rest trustworthy

Two tests gate every change:

- **`make check`** — `rules.c` has no Plus/4 in it, so the frozen
  edition's thirteen-row rule table runs against it under the host
  compiler in milliseconds. 13/13 pass. Seven executor assertions are
  still deferred (backlog 4.5).
- **`make conform`** — both editions draw the opening board headless and
  the two screenshots are compared with `cmp`. **Byte-identical.** This is
  the load-bearing test: it is what lets the renderer be rewritten in
  assembly without the rewrite being an act of faith.

The conformance test currently covers the *empty* board only. Extending it
to a mid-match position (backlog 3.11) would cover occupied squares, the
flood treatment and the piece digits, and it should be done before any of
the epic 12 speed work touches the renderer again.

## 4. Where the design earned its keep

**The blitter invariant.** Screen at `$0C00`, colour at `$0800`, exactly
`$0400` apart, and every drawing primitive walks both with one pointer and
one `SBC`. Nothing in the project relocates the video matrix, and that
single restraint is why `blit_run` is 25 cycles a cell instead of needing
a second pointer set up per call.

**Degrees, not pitches.** The ambient bed's banks name chord *degrees*, so
no pairing of a primary figure with a secondary drone can be dissonant —
consonance is a property of the notation rather than something the engine
checks at runtime. The same instinct runs through the geometry tables and
the row-address table: make the invalid state unrepresentable and there is
nothing to validate.

**One volume, treated as an instrument.** TED has two tone voices and a
single global four-bit volume. Rather than pretend otherwise, the engine
modulates that one register rhythmically over a sustained drone, which is
what a hurdy-gurdy's buzzing bridge does. The limitation became the
sound.

**Notation over code.** Songs, the chord progression, the bank figures and
now the bank *rates* live in `tools/songs.mml` and are compiled on the
host. The compiler refuses what it cannot honour — a degree outside the
chord, a rate of zero — and reports consequences that would otherwise only
be audible eight minutes into a match.

## 5. Where it did not

**Animation length had to become a decision.** On the interpreted edition
the machine's own slowness set the pace of everything, and it happened to
land where a human reads as deliberate. Compiled, the same code runs a few
hundred times faster: a curtain snaps, a trophy appears rather than being
revealed, and the lots blink instead of tumbling. Every delay in this
edition is now a stated number of raster frames. This is a real cost of
the migration and it is not visible in any benchmark.

**cc65 does not let you forget it is cc65.** Three traps are now recorded
in the source at the point they bit:

1. Never `jsr` the KERNAL jump table — the runtime keeps the ROM banked
   out and a naked call executes whatever RAM lies under it.
2. Mark TED registers `volatile` — `-Osir` will hoist a raster read out of
   a spin loop and wait for a value that can never arrive.
3. **Do not hold a pointer argument across an animation.** `trophy_draw`
   read its `winner` parameter after sixty-six frames of waiting and got
   back a string of length zero, so the engraving loop silently did
   nothing — while a build with three extra debug statements in it, and
   therefore a different stack layout, engraved correctly. The argument is
   now consumed into local storage on the first line of the function.

That third one took a long time to find, and the reason is the subject of
the next section.

## 6. The open defect

**The URBOT demo wedges.** Left running headless, a match sometimes stops
dead — the screen stays byte-identical over fifteen further emulated
minutes. Matches also frequently run to a normal finish, so it is state-
or timing-dependent.

What is established:

- Heartbeat counters written from inside each of `wait_frames`' two raster
  spin loops, and on either side of `music_service`, **all stop**. The CPU
  is not in any of them.
- A phase marker in the turn loop caught it once inside `turn_sweep` and
  once inside `throw_lots`. **Two different places in two builds** — the
  signature of memory corruption, not of a loop that will not terminate.
- It is *not* caused by servicing the music from inside `wait_frames`.
  That was the leading suspect, it was backed out, and the wedge
  reproduced without it. (The change was left backed out anyway: the music
  is now serviced from shallow call sites only, which is the arrangement
  every stable long run has been under.)
- `DBG_BOUND` assertions on the lot coordinates, the glide endpoints and
  the piece indices have not fired.
- An interrupt heartbeat next to a main-line one showed **both still
  ticking** — but in a build that did not wedge at the points sampled, so
  it settles nothing yet.

**It has since got worse.** Before the tile-shading and shimmer work the
demo reached a normal victory around 1.5e9 cycles; it now wedges before
1.5e8, early enough that `make test` no longer produces a live screenshot.
Taking the shimmer back out does not move it back, so it is the change in
memory layout rather than the feature — which is one more piece of
evidence for corruption, because a defect that relocates on every rebuild
is a defect that depends on what the linker happened to put next.

The next probes are in backlog epic 16. The reason this section exists at
all is that a review which lists four wins and no open defects is not a
review.

## 6a. The tiles, and the light on them

The frozen edition's tile painter spends **five of the eight usable
luminance steps** on a single 4x4 square — highlight 7 down to shadow 2 on
the gold. At that spread a bevel stops reading as a lit surface and starts
reading as three stripes. `SHADE_SOFT` halves it to three steps.

The room that gives back is what the shimmer moves around in: a
nine-column band of light crossing the board left to right at one column
every three frames, lifting the highlight row of the tiles it passes and
touching nothing else. It is **colour RAM only** — the screen matrix never
changes, so the worst it can do is get a colour wrong for one frame, and
it can never disturb a glyph, a piece or a plaque. Ninety-six cells a
frame off a 24-byte table built once at boot.

Note what the two changes do together: the *resting* variance halves, and
the *moving* highlight is allowed to reach the top of the range at the
beam's core. Static contrast became motion.

The luminance band is now stated once, in `plus4.h`, and the colour picker
offers 3–6 rather than 0–7. That is not a restriction imposed on the
player — it is the range everything else in the program already obeys, and
below it a piece sinks into whatever tile it is standing on.

`SHADE_LEGACY` remains the default that `board_init` installs, so `demo.c`
draws the frozen edition's board without asking, `make conform` still
passes byte-identically, and the divergence is stated at exactly one line
in `main`.

## 7. The debug build, and why it is free

`make` and `make debug` compile the same sources with the same optimiser
flags and differ by one `-DDEBUG`. Without the define, `dbg.c` is an empty
translation unit and every macro expands to `(void)0`: **3 135 bytes in
the traced build, zero bytes and zero cycles in production.**

That was the design constraint, not an optimisation afterwards. A trace
facility built as a runtime `if (debugging)` leaves a branch in every
instrumented path forever, and on a 1 MHz machine an instrument that
perturbs the thing it measures is not an instrument.

What it gives:

- a **depth column** and a **three-letter subsystem** on every line
  (`gam brd rul dic bot mus frt txt`), so a log is filterable by eye;
- a **shadow return stack** — not the 6502's, which says nothing about
  intent, but the path the program says it is on, printed joined:
  `gam>turn>sweep`. That is the question worth answering when a machine
  has stopped;
- an **injected sink**, chosen in `main`, so the same trace can scroll on
  screen for a person or be kept silently for a headless run;
- `DBG_BOUND`, which reports an index that has left its range. Without
  memory protection an out-of-bounds write does not fault, it changes
  whatever the linker put next — which is exactly why the wedge in §6
  lands somewhere different in every build.

Its own shortcoming is recorded as backlog 15.8: the panel currently
shares rows 13–24 with the casting floor and the chronicle, and they draw
over each other.

## 8. What should happen next, in order

1. **Backlog 16.6 — make the demo deterministic.** `rnd_stir()` folds the
   raster position into the seed on every keypoll, so every build plays a
   *different match* and no two builds can be compared. That is why the
   wedge has taken so long: it is not merely that instrumentation moves
   the failure, it is that instrumentation changes the game being played.
   Stub the stir out behind a define and the same match runs in every
   build; the wedge becomes bisectable instead of huntable. Nothing else
   in epic 16 is worth attempting first.
2. **Backlog epic 16** — the wedge itself. Then a canary byte after each
   `.bss` array checked once a turn, `DBG_BOUND` on every `rowtab[]`
   subscript, and the KERNAL-banking question about `jmp (old_irq)`.
3. **Backlog 3.11** — extend conformance to a mid-match position. Every
   later renderer change is riskier without it, and this project has now
   made several: the soft shading profile, the shimmer, the dimming and
   the move flash all touch the renderer and none of them is covered.
4. **Backlog 4.5** — turn on the seven deferred executor assertions, so
   the deferred count reaches zero.
5. **Backlog epic 10** — the golden race reproduced on-target. Twenty-eight
   throws for *four*, six for *zero*, 7–0. That is the strongest available
   statement that the two editions play the same game, and it is
   deterministic because there is no randomness anywhere in its path.
6. **Epic 13** — hardware. Every timing in this review is from emulation,
   and the report says so wherever it says a number.

Only after those does epic 12's speed pass make sense. The first pass is
already 194–577×; nothing about it is urgent, and each item in it risks
the conformance test that makes the port trustworthy in the first place.

---

*Released under the MIT License. Copyright (c) 2026 Paul Richeson.*
