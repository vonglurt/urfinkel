# From Interpreter to Compiler: Migrating UR ROYAL from Commodore BASIC 3.5 to Compiled 6502 on a Restored Commodore Plus/4

**Author:** Paul Richeson ([vonglurt](https://github.com/vonglurt)) — contact: paulr@sdf.org
**Date:** August 2026
**License:** MIT (see `LICENSE`)
**Project:** `urfinkel` — the compiled edition of the Royal Game of Ur (Finkel ruleset)
**Predecessor:** [`../../urroyal-basic/`](../../urroyal-basic/) — frozen, maintenance only

---

## Abstract

This report documents the measurement, planning and delivery of a
migration: the ~4,600-year-old Royal Game of Ur, implemented in Commodore
BASIC 3.5 on a hardware-restored Commodore Plus/4, re-implemented as a
compiled 6502 program. The predecessor is complete and correct but slow —
a single board draw takes **24 seconds** and a single legal-move scan
takes **2.8 seconds**, both measured on the machine against its own jiffy
clock. The stated goal was a 10× improvement in draw speed. The measured
result is **194× on the board draw, 290× on a single board square, 244× on
the per-move status update and 577× on the legal-move generator**, with
the renderer producing a **byte-identical screenshot** to the predecessor.
The whole game was then converted — 25 137 bytes against the BASIC
edition's 25 681 — and the recovered budget spent on three things the
predecessor's own documentation had ruled out on performance grounds:
character-cell motion tweening, a real dice tumble, and music during play
from a raster interrupt. The report establishes the baseline profile, explains
the three interpreter properties that produce it, records the evidence
behind the choice of development chain (cc65 with `ca65` for hot paths,
after the modern `llvm-mos` toolchain was found to have no Plus/4
platform), and argues that the interesting consequence of the migration is
not the ratio but the removal of a constraint: nearly every architectural
decision in the predecessor exists because a `POKE` costs 23 ms, and none
of those decisions is load-bearing any more. It also records the four
defects the port produced, none of which is a C bug and two of which —
an interrupt borrowing the compiler's zero-page scratch, and a direct
KERNAL call made with the ROM banked out — are the characteristic ways
this toolchain injures a C programmer.

**Index Terms** — retrocomputing, Commodore Plus/4, BASIC 3.5, cc65, 6502
cross-compilation, performance measurement, conformance testing, software
migration, raster interrupts, chiptune synthesis.

---

## I. Introduction

The predecessor project, *UR ROYAL*, is documented in its own lab report
and architecture notes. It is a complete implementation of the Finkel
ruleset with four playable modes, a rules screen, an AI that narrates its
reasoning, two-voice music, a theatre-front menu, and a self-test that
checks thirteen rules and a deterministic race on the machine. It is
written as one line-numbered BASIC 3.5 file and it is, by its own
documentation, slow:

> Drawing the board costs **1279 jiffies (~21 s)**, measured with `TI`
> around `GOSUB 1000` — roughly 1.1 jiffies per `POKE` statement, which
> is simply what interpreted BASIC 3.5 costs.
> — `urroyal-basic/docs/architecture.md` §11

That sentence is the origin of this project. "Simply what interpreted
BASIC 3.5 costs" is true, and it is also a choice: the Plus/4 is a 6502
machine, and 6502 machines do not have to be programmed through an
interpreter. The question this report answers is what the game costs when
they are not.

The brief was a 10× improvement in draw speed, with the core controller
loop moved out of BASIC into a compiled program. 10× turned out to be the
wrong order of magnitude to plan against, which is itself the most useful
finding: at 10× the existing architecture survives, and at 200× it does
not need to.

## II. Method

All timings are taken **on the machine**, not estimated from instruction
counts, and both editions are timed the same way: against the PAL jiffy
clock that BASIC exposes as `TI` and that the KERNAL interrupt increments
50 times a second. The compiled edition reads the same clock through
cc65's `clock()`.

Each benchmark repeats its operation enough times to run for several
jiffies, because one jiffy (20 ms) is coarser than any single compiled
draw. The BASIC side does not need repetition for the board draw — one
draw is 1198 jiffies on its own.

Both halves run headless under VICE `xplus4` in warp mode with a cycle
budget and an exit screenshot, so a full measurement is a few seconds of
wall clock and the result is an image of the machine reporting its own
numbers:

```sh
cd src/urfinkel
make bench        # build/basicbench.png and build/bench.png
make conform      # both editions draw the board; the pngs must match
make check        # the rule table, on the host, in milliseconds
```

The BASIC benchmark is built by patching a **throwaway copy** of the
frozen source — line 300, the head of the stage intro, becomes a jump into
an appended block — so the boot-time tables are built, nothing else runs,
and the frozen edition is never modified. This is the same technique its
own `make ruletest` uses.

### A. What is measured

Five primitives, chosen because they are what a turn actually spends its
time in:

| Primitive | BASIC routine | What it is |
|---|---|---|
| board draw | `1000` | the whole playfield, once per match |
| one button | `7200` | one 4×4 shaded square — the animation unit |
| token rows | `7650` | the per-move status update on both plaques |
| chronicle line | `8000` | one line of text into the four-line log |
| legal moves | `3500` | the rule engine's per-turn scan |

The last one is not rendering at all, and it is included because a
profile should be allowed to surprise the person taking it. It did.

## III. Results: the baseline

Measured on VICE `xplus4` 3.10, PAL, from `build/basicbench.png`:

| Primitive | Reps | Total | **Per call** |
|---|---:|---:|---:|
| board draw | 1 | 1198 jif | **23 960 ms** |
| one 4×4 button | 20 | 594 jif | **594 ms** |
| both token rows | 10 | 913 jif | **1 826 ms** |
| one chronicle line | 10 | 202 jif | **404 ms** |
| legal-move scan | 20 | 2785 jif | **2 785 ms** |

The board figure of 1198 jiffies agrees with the predecessor's own
documented 1279 within the difference in what is on the board when the
measurement is taken, and confirms its ~1.1 jiffies-per-`POKE` estimate:
the draw issues roughly 1030 `POKE` statements, for **23 ms per POKE**.

### A. The finding that was not expected

**The rule engine is the most expensive thing in a turn.** One
`GOSUB 3500` — a 7×7 nested loop with no drawing in it at all — costs
2.79 seconds, more than a full status repaint and nearly five times a
single square. Every turn calls it once, and the self-test's dice driver
calls it up to four more times while searching for a legal throw.

Composed into one representative turn (one move scan, two token-row
refreshes, six square repaints for a three-step animated move, and five
chronicle lines of URBOT deliberation):

```
  legal-move scan          2 785 ms
  token rows      x2       3 652 ms
  square repaint  x6       3 564 ms
  chronicle       x5       2 020 ms
                          ---------
  a turn                  12 021 ms      about twelve seconds
```

That is the real answer to "why does it feel slow", and it is not the
board draw — the board draw happens once per match and is covered by the
theatre framing. It is that every single turn spends twelve seconds
somewhere, of which a quarter is spent deciding which moves are legal.

## IV. Why BASIC 3.5 costs this

Three properties of the interpreter account for essentially all of it.
The predecessor's architecture notes identify all three and work around
all three; the workarounds are good and they are worth about 12%.

1. **There is one numeric type, and it is a float.** BASIC 3.5 stores
   every number as a five-byte floating-point value. `P(cp,k)=np` is two
   array-descriptor lookups, two integer-to-float conversions and a
   floating-point comparison — for a byte compare against a value in the
   range 0–15. This is why the legal-move scan is the most expensive
   primitive: it is almost pure array arithmetic and therefore almost
   pure float conversion.

2. **The variable table is scanned linearly, in creation order.** Every
   reference to a name costs a walk of the table until it matches. The
   predecessor's lines 100–108 exist solely to create the hot inner-loop
   names first so they sit near the front — a real optimisation, and one
   that has to be maintained by hand forever.

3. **Nothing is constant-folded, and every statement is re-parsed.**
   `POKE SC+20*40+I,160` re-evaluates `20*40` in floating point on every
   iteration. Hoisting row addresses into variables took the predecessor's
   board draw from ~1560 jiffies to ~1390 — a genuine 11% for a
   pervasive change in how the drawing code is written.

A compiler removes all three at once. Bytes stay bytes, names become
addresses at link time, and `20*40` is folded before the program ever
runs.

## V. Choosing the development chain

Four options were considered. The decision rests on one hard fact and one
soft one.

### A. llvm-mos — rejected on availability

`llvm-mos` is the modern LLVM-based 6502 compiler and generates
substantially better code than cc65. Its SDK ships platform support for
`c64`, `c128`, `pet`, `vic20`, `cx16`, `mega65`, the Atari 8-bit line,
NES, Lynx, PCE and others — and **no `plus4` or `c16` platform** [1]. A
Plus/4 port would mean writing the platform layer, the startup code and
the linker configuration before writing a line of the game. Rejected: the
project is a game, not a toolchain port.

### B. A native BASIC compiler — rejected on ceiling

Compiling `urroyal.bas` roughly as-is with a Plus/4 BASIC compiler is by
far the cheapest path and keeps a single source. But such compilers
compile *BASIC*: floats stay floats and the variable model stays the
variable model, so the realistic gain is 3–5×, not two orders of
magnitude, and §III shows the dominant cost is exactly the float array
arithmetic a BASIC compiler preserves. The toolchain also runs on the
target machine rather than on the host, which puts it outside git and
outside `make`.

### C. Pure `ca65` assembly — rejected on cost, retained for hot paths

Hand-written 6502 is another ~5–10× over cc65's C output and produces the
smallest binary. It also makes the rules engine — the part that must stay
conformant with a written specification and be readable by a human a year
later — the hardest part of the program to read, and it forecloses
host-side testing entirely. Rejected as a whole-program strategy;
**adopted for the blitter**, where the work is a tight loop with no logic
in it.

### D. cc65 with `ca65` hot paths — chosen

cc65 has a first-class `plus4` target (`plus4.cfg`, `plus4.lib`), is one
`brew install` away, and was already named as the intended path in this
repository's own toolchain notes [2]:

> `cc65` is a full C compiler, assembler, and linker for 6502 targets, and
> it has explicit Plus/4 platform support… It is the right tool if this
> project ever moves from BASIC into native code.
> — `docs/06-macos-toolchain.md` §2.1

The deciding soft factor is testability. C that avoids machine specifics
compiles for **both** the Plus/4 and macOS, so the ruleset can be tested
by the host compiler in milliseconds while the on-target self-test remains
the final word. That property does not survive either of the rejected
alternatives, and §VII shows what it is worth.

### E. The chain

```
   src/*.c ──── cl65 -t plus4 -Osir -Cl ────► build/*.prg
      │                                            │
      │                                     c1541 ─┴─► .d64 ──► xplus4
      ├──── cc -std=c99 (host) ──► make check              └──► SD2IEC ──► Plus/4
      │        rules only, milliseconds
      │
   src/blit.s ── ca65 ──► the per-cell inner loop

   ../urroyal-basic/urroyal.bas ── petcat -w3 ──► the conformance oracle
```

Tool versions used for every number in this report: cc65 2.19 (the binary
self-reports `V2.18`), VICE 3.10, macOS 26.5.2, PAL timing throughout.

## VI. Results: the compiled edition

From `build/bench.png`, built with `cl65 -t plus4 -Osir -Cl`:

| Primitive | BASIC 3.5 | Compiled | **Speedup** |
|---|---:|---:|---:|
| board draw | 23 960 ms | **123.4 ms** | **194×** |
| one 4×4 button | 594 ms | **2.05 ms** | **290×** |
| both token rows | 1 826 ms | **7.48 ms** | **244×** |
| legal-move scan | 2 785 ms | **4.83 ms** | **577×** |
| one chronicle line | 404 ms | *not ported* | — |

The representative turn from §III‑A, excluding the chronicle:

```
  BASIC 3.5    10 001 ms
  compiled         32 ms          311x
```

The brief asked for 10×. The delivered first pass is between 194× and
577× depending on the primitive, and the ordering is informative: the
further a primitive is from raw memory writes and the deeper it is into
array arithmetic, the larger the gain, because that is where the
five-byte float hurt most.

### A. Optimiser settings are worth 20%, not 20×

Measured on the identical source:

| Flags | Board draw |
|---|---:|
| `-O` | 151.0 ms |
| `-Osir -Cl` | **123.4 ms** |

`-Osir` is cc65's full optimiser and `-Cl` makes local variables static
rather than stack-allocated, which matters on a 6502 because the software
stack is expensive to index. Worth taking, and worth keeping in
perspective: the compiler is not where the two orders of magnitude came
from. The type system is.

### B. Where the remaining time goes

The board draw writes about 2030 cells (a 1000-cell screen clear plus the
~1030 cells of table, buttons and plaques) in 123.4 ms — **60 µs per
cell**, or roughly 54 cycles at the Plus/4's ~0.886 MHz screen-on clock.
The assembly blitter's inner loop is 25 cycles per cell. **Rather more
than half the remaining time is therefore C-side overhead**, not the
memory writes: cc65's six-argument call into `paint_button`, and the 90
single-cell `blit_run(1)` calls the gutter pass makes, each paying full
pointer setup for one byte of work.

That is the headroom, and it is known rather than hoped for: moving
`paint_button` and the gutter pass into `blit.s` should approach the
25-cycle floor, taking the board draw toward ~50 ms. It is deliberately
**not** done yet, because 123 ms is already 194× and correctness comes
first.

## VII. Conformance

Speed that changes behaviour is not a migration, it is a rewrite. Two
mechanisms hold the compiled edition to the frozen one.

### A. The screen, compared as an image

Both editions draw the opening board with the same names, hues and
shades, headless, and the screenshots are compared byte for byte:

```
$ make conform
conform ok: compiled board is pixel-identical to basic
```

Both PNGs hash to `75b042780b045cdd5edf442f96e1e13a`. This is the
acceptance test for every renderer change from here on: the compiled port
is allowed to be faster, not different.

The test earned its keep immediately. The first compiled board differed
in two ways, both invisible in the source and obvious in the image:

1. cc65's startup leaves the machine in the **lower-case character set**,
   where screen codes 87, 81 and 170 are the letters `w`, `q` and a plain
   asterisk rather than the waiting ring, the home disc and the rosette's
   reverse star. The game assumes the upper-case/graphics set a Plus/4
   boots into. One `cbm_k_bsout(142)`.
2. The TED **background register** was never set to black. Because every
   engraved element in this game is reverse video, and reverse video
   renders its glyph in the *background* colour, the plaque lettering and
   the rosette stars were being punched in pale violet instead of black.
   One store to `$FF15`.

Neither is a logic error and neither would have been caught by a rule
test. Both were caught by a `cmp` of two PNGs.

### B. The rules, compared against the frozen table

`src/rules.c` contains no screen address, no TED register and no cc65
extension, so it compiles for the host. The thirteen-row rule table from
the frozen edition's mode 6 (`urroyal.bas` line 9600) is carried across
verbatim and run by the system compiler:

```
$ make check
13 of 13 generator checks passed
7 executor assertions deferred until move.c lands
```

Milliseconds, against about a minute for the same checks under emulation.
The seven deferred assertions are the rosette-flag and capture-count
checks, which need the move executor; they are carried in the table
already, unasserted and counted, so that landing the executor drops the
deferred count to zero rather than requiring the table to grow.

The on-target self-test is **not** replaced by this. The host test is the
fast inner loop; the machine remains the final word, because only the
machine exercises the machine.

## VIII. Discussion: what the speed actually buys

The ratio is not the interesting part. This is: the predecessor's
architecture is a set of intelligent responses to a 23 ms `POKE`, and
every one of them can now be reconsidered.

| Predecessor decision | Why it existed | Status now |
|---|---|---|
| Paint gutters only (90 cells, not 306) | a full flood cost 306 POKEs | unnecessary; the flood is 18 ms |
| Split plaque painters (`7700` whole, `7650` tokens only) | a full plaque repaint was ~4 s | unnecessary; the whole plaque is under 1 ms |
| No double buffer; localised repaint only | a full-screen repaint was unaffordable | now affordable, and TED can page-flip |
| Hoist row addresses out of every loop | no constant folding | done by the compiler |
| Declare hot variables first (lines 100–108) | linear variable-table scan | meaningless; names are addresses |
| Delays as calibrated `FOR` loops | there was nothing else | should become raster/IRQ timing |
| Music only at victory | no spare cycles during play | an IRQ player is now routine |
| Piece choice by number key, not a cursor | a cursor layer could not be kept responsive | affordable; it is in the icebox |

The last two are features the predecessor's own documentation lists as
out of scope *specifically* on performance grounds — background music
during play needing "a machine-language IRQ player (`SYS`), which is out
of scope for the pure-BASIC constraint", and joystick cursor selection.
Both become ordinary work.

The design constraint has inverted. The question is no longer "how few
cells can I touch" but "what should the machine do with the other 99.5%
of its time".

## IX. Spending it: the playable conversion

§VIII argues that the removed constraint matters more than the ratio. This
section is the evidence, because the whole game was then ported and three
things the frozen edition explicitly ruled out on performance grounds were
built.

The compiled game is **25 137 bytes** against the BASIC edition's 25 681,
and plays: four modes, the opening ceremony, tumbling lots, the move
executor, URBOT with its four doctrines narrating into the chronicle, the
waist plaques, the colour picker, the rules screen, the victory sequence,
the theatre front it is all played inside and the gold trophy at the end
of it.

### A. Motion tweening

The frozen edition moves a piece **one board square per step**, because a
square is 32 `POKE`s and a `POKE` was 23 ms — anything finer was not
available. At 2.05 ms a square there is room to move one **character
cell** at a time, so `glide()` walks the straight line between two squares
saving and restoring each cell it covers. Five steps where there was one,
one frame apart, and the piece travels instead of teleporting.

The same budget bought a real dice tumble. The frozen edition ran **one to
three** tumble frames; this one runs **sixteen to twenty-one**, with the
lots scattering across the whole casting floor under the same rejection
sampling and settling one at a time.

### B. Music during play

The frozen edition's architecture notes are explicit that this was out of
scope:

> Truly steady music under all load needs a machine-language IRQ player
> (`SYS`), which is out of scope for the pure-BASIC constraint — the
> cooperative design is the icebox item.
> — `urroyal-basic/docs/architecture.md` §9

Both halves of that sentence are now available, and the engine uses both.
A **raster interrupt at 200 Hz** (four slots a frame) writes the sound
registers; a **cooperative 50 Hz sequencer** in C decides what they should
say and is called from every wait loop in the game, so the bed keeps
playing while the game waits on a human.

The design is dictated by one hardware fact: **TED has two tone voices and
a single global four-bit volume.** There is no per-channel volume, so a
quiet pad under a loud melody is not physically available. What is
available, and is what the engine does: arpeggiate chords on voice 1 fast
enough that the ear fuses them, hold a drone on voice 2, and modulate the
one global volume rhythmically — which over a continuous drone is
precisely a hurdy-gurdy's buzzing bridge. The constraint is the
instrument. Full design in [`music.md`](music.md).

Two details worth recording as results rather than as decisions:

- The arpeggio cycles **3 steps of 8 ticks** and the buzz **5 steps of 13
  ticks**. 8 and 13 are adjacent Fibonacci numbers and therefore coprime,
  so the periods (24 and 65 ticks) do not realign for about eight seconds
   — genuine polyrhythm out of two table lookups.
- The chord root advances **a fifth every three seconds**, so the bed
  walks the whole circle of fifths in thirty-six seconds, alternating
  major and minor on the way.

**The bed rotation, measured.** A written bed does not loop; reaching its
end hands over to the next, wrapping among the *written* beds only - the
generated bed and silence have no end, so handing to either would stop the
rotation dead. Measured with a probe that records the frame of every
handover, against the lengths summed from `song.h`:

| bed | notes | computed | measured | latency |
|---|---:|---:|---:|---:|
| hurrian, dumbrill | 33 | 816 | 818 | +2 |
| lullaby | 47 | 784 | 786 | +2 |
| hurrian, kilmer | 166 | 2760 | 2762 | +2 |
| seikilos | 37 | 1225 | 1227 | +2 |

A full rotation is 5593 frames, **1 min 52 s**. The two-frame latency is
the sequencer noticing.

Getting those four numbers cost a bug worth recording, because it is the
same shape as the interrupt one: **a saved value that fed itself.**
`music_song` stores the bed's position whenever a non-looping song starts
over a running bed, which is what puts the bed back after a turn cheer.
But a bed handing over to the *next* bed goes through the same call, and
arrives from inside `song_frame` with the old song sitting on its own
`SONG_END`. `bed_song` read that back as a resume point and seeked the
incoming bed straight to it, so the new bed ended on its next frame and
handed over again. Measured before the fix: the first handover at frame
818, and the next five **two frames apart** - the whole rotation gone in a
dozen frames. `music_bed` clearing the flag first did not help, because
the clear happened before the write that undid it. `bed_song` now takes
its copy of the resume point *before* the call that can overwrite it.

Neither this nor the rotation could be observed at all until the raster
interrupt was fixed (§IX-D5): with the frame counter stuck, the sequencer
never ran a frame.

### C. Songs as text

Set pieces are written in a small notation and compiled on the host:

```
song theme
  v1  d5:24 e5:12 f5:24 e5:12 | d5:24 c5:12 a4:12 d5:36
  v2  f4:24 g4:12 a4:24 c5:12 | b4:24 a4:12 f4:12 f4:36
```

`make music` turns `tools/songs.mml` into `src/song.h`; the player never
changes. Pitch goes through the same `N = 1024 − 110841/Hz` the BASIC
edition used, so the two editions are in tune with each other, and the
compiler reports which notes it had to lift an octave to clear TED's
~108 Hz floor.

### D. Four bugs the port produced, and what each cost to find

Recorded because they are the actual character of this toolchain, and none
of them is a C bug:

0. **Never read-modify-write `$FF12`.** It carries voice 1's top two
   frequency bits *and the character generator's configuration*. Doing the
   obvious RMW 200 times a second latched a bad read minutes into a game
   and turned every glyph on the screen to garbage — which looks exactly
   like a renderer bug. Snapshot the upper bits once and merge against
   that. Found by bisecting subsystems, not by reading the code.
1. **cc65's startup leaves the lower-case character set selected**, where
   screen codes 87, 81 and 170 are `w`, `q` and `*` rather than the
   waiting ring, the home disc and the rosette star. Found by `make
   conform` in one screenshot diff.
2. **The TED background was never set to black.** Every engraved element
   in this game is reverse video, and reverse video renders its glyph in
   the *background* colour, so the plaque lettering came out pale violet.
   Also found by the screenshot diff.
3. **The interrupt borrowed cc65's zero-page `tmp1`.** cc65's software
   stack and its scratch locations are shared with the main line, so an
   interrupt that touches them corrupts whatever C was doing. The handler
   now has its own byte.
4. **`jsr $FFE4` does not work.** cc65's plus4 runtime keeps the KERNAL
   ROM banked out except around its own wrappers, so a naked call to the
   jump table executes whatever RAM lies under it — which here scribbled
   over the screen on its way to wedging the machine. The symptom (eleven
   characters missing from one menu row) looked like a renderer bug and
   was not. The keyboard is read through `conio`'s `kbhit`/`cgetc`.

5. **An interrupt handler on `$0314` cannot read its own variables.** The
   same banking fact as number 4, on the other side of the call. cc65's
   plus4 runtime runs the program with RAM switched in and its own stub on
   the RAM `$FFFE`; that stub switches the **ROM back in** before it reaches
   `$0314`, because that is where the KERNAL's interrupt code lives. cc65
   puts BSS at the end of the program, and once UR FINKEL passed ~30 KB
   that landed above `$8000` — so the handler's variables were being read
   from BASIC ROM and written to the RAM underneath. `inc _music_frames`
   read the ROM byte at its address, added one, and stored that, so the
   frame counter sat at ROM+1 for ever and the sequencer, the attract
   timeout and the demo all stalled behind it. The arpeggio tables and
   `miscshdw` were equally fictional, which is where number 0's garbage in
   `$FF12` was really coming from.

   Confirmed by arithmetic rather than by inference: a probe read
   `music_frames` = 33, `dbg_ent` = 22 and `dbg_our` = 21, and
   `basic-318006-01.bin` holds **32, 21 and 20** at those three addresses.
   A fourth counter that the handler never incremented read its true RAM
   value of 0 rather than ROM+1.

   Because it depends on where the linker happens to put BSS it appeared
   and vanished as unrelated code was added — a debug build with a few more
   statements in it ran to a finish — which is why it read as a timing
   fault for weeks. The fix is to take the processor's own vector at
   `$FFFE` in RAM and never be entered through the KERNAL at all;
   `irq.s`'s header states the rule.

Numbers 3, 4 and 5 are the ways a C programmer gets hurt on this target,
and all of them are invisible to the compiler. Four and five are the same
fact — *the KERNAL is not there unless the ROM is switched in, and while
the ROM is switched in our own memory is not there either* — met once
going out and once coming in.

### E. A note on the harness

VICE's autostart is **not deterministic**: about one run in eight it loses
the `RUN` keystroke after `LOADING` and leaves the machine at `READY`.
Measured over 8 runs per configuration — plain `-autostart` 6/8,
`-autostartprgmode 1` 0/8, `-autostartwithcolon` with a fixed delay 7/8.
No setting makes it certain, so `tools/viceshot.py` verifies the result
instead of hoping for it and retries: every program this project
screenshots ends with the TED background at true black, and the BASIC boot
screen has essentially no black in it at all.

## X. Threats to validity

- **The measured primitives were measured in isolation.** The whole game
  is now ported (§IX) and plays, but §VI's ratios are per-primitive.
  Nothing in this report measures a full assembled turn end to end, only
  its components.
- **The chronicle has not been re-benchmarked.** It is ported and it
  works, but no compiled figure has been taken to set against BASIC's
  404 ms per line, so it is still excluded from the composite rather than
  estimated. That is the next measurement to take.
- **The golden race has not been reproduced yet.** The compiled edition
  passes the thirteen generator checks on the host; the deterministic
  end-to-end race (28 throws for *four*, 6 for *zero*, 7-0) needs the
  self-test driver ported, and until it is, "the two editions play the
  same game" is supported but not demonstrated.
- **All numbers are from emulation.** VICE is cycle-accurate enough that
  the ratios should hold, but the predecessor's own specification list
  still carries an open item to re-verify on the restored hardware via
  SD2IEC, and this project inherits it.
- **`clock()` reports NTSC.** cc65 hardcodes `CLOCKS_PER_SEC` to 60 for
  CBM targets; the machine and VICE are both PAL. Raw jiffies are used
  throughout and divided by 50 by hand, so the constant is never
  consulted — but code that trusts it will be wrong.
- **The composite "twelve second turn" is a composition, not a
  measurement.** It sums independently measured primitives at plausible
  counts. It is a profile, not a stopwatch reading.

## XI. Conclusion

A 10× target was set against a program whose slowest primitive is 2.8
seconds long. The measured first pass is 194–577× on four of the five
primitives profiled, with a byte-identical screen and thirteen of thirteen
rule checks passing on a host build that runs in milliseconds. The
development chain is cc65 with `ca65` for the per-cell inner loop, chosen
after `llvm-mos` was found to have no Plus/4 platform and a BASIC compiler
was found to preserve exactly the cost that dominates the profile.

The BASIC edition is frozen at `src/urroyal-basic/`, not deleted: it is
the rules specification, the conformance oracle, and the only edition that
can be typed into the machine itself.

What remains is the game — the controller loop, the move executor, the
chronicle, URBOT, the theatre front and the music — ported onto a machine
that now has time to spare. The ordered plan is in
[`../backlog.md`](../backlog.md); the target design is in
[`architecture.md`](architecture.md).

## References

[1] llvm-mos, *llvm-mos-sdk* platform directory listing. Platforms
    present: atari2600, atari5200, atari8, c128, c64, commodore, cpm65,
    cx16, dodo, eater, fds, geos-cbm, lynx, mega65, neo6502, nes, osi-c1p,
    pce, pet, rp6502, rpc8e, sim, supervision, vic20. No plus4 or c16.
    https://github.com/llvm-mos/llvm-mos-sdk/tree/main/mos-platform

[2] This repository, *§VI — macOS Host Toolchain*, `docs/06-macos-toolchain.md` §2.1.

[3] cc65, *Commodore Plus/4 specific information for cc65*.
    https://www.cc65.org/doc/plus4.html

[4] P. R., *From Viking Festival to Video Terminal*, the predecessor's lab
    report, `src/urroyal-basic/docs/lab-report.md`.

[5] P. R., *UR ROYAL — Architecture* §11, Performance notes,
    `src/urroyal-basic/docs/architecture.md`.

---

## Appendix A — Maintenance log

This report is maintained, not archived. Every measurement in it is
reproducible with `make bench`, `make conform` and `make check`, and any
epic that changes a measured number must update the table it appears in
and add a row here.

| Date | Change | Effect on the numbers |
|---|---|---|
| 2026-08-06 | Baseline established; renderer and move generator ported; chain chosen | §III and §VI tables created; conformance and host checks both green |
| 2026-08-07 | Binary size re-taken after the cabinet, the trophy, the music engine and the trace facility landed | §Abstract and §IX: 19 413 → **25 137 bytes**. The §VI speed table is unaffected — none of those additions is in a timed path. Tumble length corrected in §IX‑A to the 16–21 frames `dice.c` actually runs |
| 2026-08-07 | Raster interrupt moved from the KERNAL's `$0314` to the processor's `$FFFE`, so it is entered with RAM rather than ROM switched in | §IX‑D gains bug 5. The interrupt now counts **199 frames in 200** raster frames and takes four entries a frame, both measured in the running game; before the change the counter never moved at all. `make check` 13/13 and `make conform` pixel-identical are unaffected |

**Numbers that must be re-taken when touched:**

| If you change… | Re-run | Update |
|---|---|---|
| anything in `src/blit.s`, `board.c` | `make bench`, `make conform` | §VI table, §VI‑B analysis |
| anything in `src/rules.c` | `make check`, and the frozen edition's `make ruletest-shot` | §VII‑B, and the golden race |
| the cc65 flags in the Makefile | `make bench` | §VI‑A table |
| the frozen edition (bug fix only) | `make conform`, `make bench` | §III table if the baseline moved |

---

*Released under the MIT License. Copyright (c) 2026 Paul Richeson.*
