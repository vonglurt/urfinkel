# UR FINKEL — the Royal Game of Ur for the Commodore Plus/4, compiled

## How to play

Two builds. **Right-click → Save Link As** — a plain click may open the file
in a browser tab rather than saving it.

| Download | What it is | Size |
|---|---|---:|
| [**urfinkel.d64**](https://github.com/vonglurt/urfinkel/raw/main/build/urfinkel.d64) | a **disk image** with the program inside; on the machine, `dload"urfinkel"` | 174 848 B |
| [**urfinkel.prg**](https://github.com/vonglurt/urfinkel/raw/main/build/urfinkel.prg) | the **program** by itself, ready to attach or drag in | 56 511 B |

Take the `.d64` if your emulator wants a disk, the `.prg` if it will accept a
bare program. The `.prg` loads inside the machine's 64K, so tape or disk both
have room for it.

### In a browser

[**plus4.cybernoid.xyz**](https://plus4.cybernoid.xyz/) runs a Plus/4 in a
browser tab — no install, no login. Save one of the files above, then
**ADD MEDIA** → choose it from your computer → double-click the entry to load
and run it. Either file works; the `.prg` starts faster.

### On the desktop

**VICE** (`xplus4 urfinkel.d64`), **YAPE**, or **plus4emu** — all three take
either file. Build-from-source instructions are further down.

The compiled edition of [UR ROYAL](../urroyal-basic/): the same ~4,600-year-old
Royal Game of Ur, the same Finkel ruleset, the same screen — written in C
and 6502 assembly instead of interpreted BASIC 3.5, because the BASIC
edition takes **24 seconds to draw the board** and **2.8 seconds to work
out which moves are legal**.

The predecessor is complete, correct, and frozen at
[`../urroyal-basic/`](../urroyal-basic/). It stays as the rules
specification, the conformance oracle, and the only edition that can be
typed into the machine itself. This one is where development happens.

## Where it stands

**It plays, inside a cabinet.** Boot lands on a theatre front — frieze
bands, three rails of chasing lamps, a proscenium — and the curtains open
on a royal fanfare to reveal the menu. Four modes (vs URBOT, two players,
two players with real lots, URBOT demo), a rules screen, the opening
ceremony, tumbling tetrahedral lots, the full Finkel ruleset, URBOT
narrating its doctrine turn by turn, the waist plaques, a colour picker
with a live preview, and a victory that pours a gold trophy and engraves
the winner's name into its bowl — in **29 374 bytes**, against 25 681 for
the BASIC edition. The compiled program is *larger*; the speed bought
animation, a music engine and a keyboard driver, not size.

**It runs on the real machine.** Loaded from an SD2IEC onto a restored
PAL Plus/4 on 2026‑08‑07: menu selection, music cycling, both prompts and
the exit all answered. That is the project's first hardware validation of
any kind — every other number here is from emulation.

**The board says whose turn it is without words.** The side that is not to
move recedes three luminance steps — squares, plaque and piece count — and
the shimmer stops crossing it. And a piece no longer appears on the next
square: the one it leaves goes black, flashes white and comes back to its
own colour while the one it arrives at flashes white and settles into the
mover's. A square merely jumped over keeps its colour, because nothing
about it changed hands; a capture goes red, white, red, black and then the
winner's colour.

**The two moments that stop the game take the whole bottom of the
screen.** A capture and a win used to be a word cut into one row of the
eleven below the board, for two seconds each — while the dice, which
happen every turn, had the other ten rows and one and a half. Both now
own rows 14–24, the casting floor *and* the four lines of the chronicle,
which is affordable only because the log can repaint itself from its own
buffer.

A capture sets the place on fire: a bed of flame along the bottom six
rows, rippling sideways on a wave and breathing up and down on an
envelope, with `captured` standing over it in block letters four columns
wide and five deep. The letters do not change colour — they *ignite*,
each cell morphing through a speck, a lump of flame, a solid block and
back out again, and the ignition climbs from the bottom row upward
because that is where the fire is. Four seconds.

A win gets the winner's name cut by the laser at the same size — eight
letters at a five-column pitch is thirty-nine of the forty columns, and
eight is the length of the name field, which is where the size came
from — then the gold cup, then a firework display with three shells in
the air at once and the border flashing each burst's own hue. Twelve
seconds, and the only twelve seconds in the match nobody is in a hurry.

The tiles are lit differently from the frozen edition on purpose. Its
painter spends five of the machine's eight luminance steps on one 4×4
square; this one spends three and then drops the whole band two steps
further, because the board is now a **black grid** — separator rows
between the bands, black gutters between the tiles, black flanks either
side of the plaques — and black only reads as an edge if what sits inside
it is dark enough for black to be a step down.

Over that runs the **shimmer**: on every tile a bright point travels
*round* the face, left to right along the lit top edge and right to left
along the shadowed bottom edge. A highlight that moves around an object
rather than across it is the cue the eye uses for a curved surface
catching a moving light, which is why this reads as depth where the
earlier left-to-right sweep read as a passing cloud. Every tile orbits on
the same eight-step cycle but starts at a different point in it, offset by
where it sits, so crests still drift across the board as a whole — the
field behaviour of the old sweep, out of per-tile motion rather than
imposed on top of it.

It touches only the top and bottom row of each tile, never the two rows
between them: those carry the well, and the well carries pieces, rosette
stars and the flood colour of whoever is standing there. Verified by
diffing two frames — the changed pixels fall in exactly character rows 0,
3, 5 and 8, and the dimmed band does not change at all. Colour RAM only,
so it can never disturb a glyph.

Still to come: the on-target self-test that would reproduce the frozen
edition's golden race, and joystick selection.

**One defect still open, and its status has changed.** The URBOT demo
used to wedge: the machine stopping dead with the screen byte-identical
for as long as you left it. A defect certainly present has since been
removed — the raster interrupt was reading every one of its own variables
out of BASIC ROM, because cc65's stub switches the ROM in before it
reaches `$0314` and the linker had put `irq.s`'s BSS above `$8000`. With
that fixed the demo runs, `make test` passes, and a match ran to 9e8
cycles still advancing, against the old "wedges before 1.5e8".

That is **not** the same as saying the wedge is fixed. It is one long run,
and it is not obvious how that fault would stop a processor rather than
merely make the timers fictional. The honest position, recorded in backlog
16.11, is that the wedge has not been reproduced since and the test that
would settle it — a fixed seed, then the same match at several cycle
budgets — has not been run.

## The measurement

Taken on the machine, both editions against the same PAL jiffy clock, with
`make bench`:

| Primitive | BASIC 3.5 | Compiled | Speedup |
|---|---:|---:|---:|
| draw the board | 23 960 ms | **123.4 ms** | **194×** |
| paint one 4×4 square | 594 ms | **2.05 ms** | **290×** |
| refresh both token rows | 1 826 ms | **7.48 ms** | **244×** |
| find the legal moves | 2 785 ms | **4.83 ms** | **577×** |

A representative turn's rendering and rule work goes from about **10
seconds to about 32 milliseconds**.

The brief was 10×. The full reasoning, the baseline profile, why BASIC 3.5
costs what it does, and why the ratio matters less than the constraint it
removes are in [`docs/lab-report.md`](docs/lab-report.md).

## What the speed was spent on

- **Motion tweening.** The BASIC edition moved a piece one whole board
  *square* per step, because a square is 32 `POKE`s and a `POKE` was
  23 ms. Here the glyph travels one **character cell** at a time along
  the line between squares — five steps where there was one.
- **A real dice tumble.** One to three frames became sixteen to
  twenty-one, the lots scattering across the whole casting floor and
  settling one at a time.
- **Music during play.** The BASIC edition's own notes rule this out:
  steady playback "needs a machine-language IRQ player (`SYS`), which is
  out of scope for the pure-BASIC constraint". It now has one — a raster
  interrupt at 200 Hz — plus a cooperative 50 Hz sequencer that every
  wait loop in the game calls, so the music keeps playing while the game
  waits for you.

## The music

TED has **two tone voices and a single global four-bit volume**. There is
no per-channel volume, so a quiet pad under a loud melody is not something
this machine can be made to do. The engine is built on that rather than
against it:

- chords are **arpeggiated**, fast enough that the ear fuses them —
  **both** voices, from the same chord, restarting together on the
  downbeat;
- the one global volume **does not move**. Modulating it was tried three
  ways — an attack envelope, a hurdy-gurdy buzz, effects seizing the level
  for priority — and every one of them moved both voices at once, because
  there is only one register. Expression is in pitch.

The generated bed is built on a grid. The chord changes every **bar, 3.2
s**, through the eight-bar progression `i bVII IV v i bVII bIII v`. The
whole progression then transposes **up a fifth every phrase, 25.6 s**, so
the bed walks all twelve keys in **5.1 minutes**; the mode flips every
second phrase, 51.2 s, which is the largest gesture it makes. Over that,
the secondary voice keeps the pulse: it strikes the bar's own bass on
**1, 3 and the and-of-3** and rests for the other thirteen sixteenths,
rather than holding a drone through the bar. The high sparkle layer that
used to punctuate it is gone — it fired near the top of TED's range and,
because effects pre-empt voice 1, punched a hole in the melody to do it.

The four **written** beds — two Hurrian hymns, a lullaby, and the Seikilos
epitaph — do not loop. Each hands over to the next when it ends, wrapping
among the written ones only, so the music moves along by itself. Lengths, summed from the notation and
confirmed against measured handovers to within two frames: hurrian 16.3 s,
lullaby 15.7 s, kilmer 34.6 s, seikilos 24.5 s — a full rotation in
**1 min 31 s**. `m` cycles all six choices including the generated bed and silence.

Set pieces are **written as text** and compiled on the host:

```
song theme
  v1  d5:24 e5:12 f5:24 e5:12 | d5:24 c5:12 a4:12 d5:36
  v2  f4:24 g4:12 a4:24 c5:12 | b4:24 a4:12 f4:12 f4:36
```

The generated ambient bed is notated in the same file — a chord
progression, and four banks each for the arpeggiating voice and the
sustaining one, written as chord *degrees* so no pairing of banks can be
dissonant:

```
progression  i bVII IV v i bVII bIII v
bank primary c  1 3 5 8 5 3    up 12  div 3
bank second  b  1 5
```

`make music` turns `tools/songs.mml` into `src/song.h`; the player never
changes. Full design in [`docs/music.md`](docs/music.md).

## The keyboard

The one subsystem that did not survive the migration. In BASIC it is a
single token — `GET` — used thirteen times, and it works because the
interpreter runs underneath a live operating system whose periodic
interrupt scans the matrix and fills a buffer. Compiled under cc65 that
operating system is in ROM but **not running**: TED's interrupt mask reads
`$A2`, only the raster bit set, and the KERNAL's jiffy clock is frozen in
all three bytes. So `$EF` is never filled, `kbhit()` can never return
true, and no key can ever reach the program. Nothing was miscoded; the
runtime changed underneath an API whose signature says nothing about what
maintains it.

`src/kbd.c` reads TED's matrix directly — row mask to `$FD30`, strobe
`$FF08`, read the columns back active low — and owes the KERNAL nothing.

It then failed a second time, for a reason with no representation in the
function that contained it: the debounce asked a key to read down on two
consecutive **scans**, and nothing fixed the scan rate. A bare spin
scanned thousands of times a second, so it debounced nothing and produced
phantom keypresses; the menu scanned once every third of a second, so an
ordinary tap was never seen twice and never arrived. One mistake, two
opposite symptoms. The scan is now driven once a frame from the one
shallow wait everything passes through, which makes the rate 50 Hz
everywhere and the debounce a flat 40 ms.

Written up as a report in
[`docs/keyboard-lab-report.md`](docs/keyboard-lab-report.md).

## Controls

| Key | Where | What |
|---|---|---|
| `1`–`5` | menu | choose a mode |
| `m` | menu, and both in-game prompts | cycle the music: off, the generated bed, and the four written ones |
| `0`–`4` | manual mode | type the tip count yourself |
| `1`–`7` | any turn | choose which piece to move |
| `x` `x` `x` | any match | leave and return to the cabinet — three in a row, and any other key resets the count |
| space | after a win, and the rules screen | continue |

Left alone for two minutes, the cabinet starts the URBOT demo by itself.

## Two builds

```sh
make            # build/urfinkel.prg      production
make debug      # build/urfinkel-dbg.prg  traced
```

Same sources, same optimiser flags, one `-DDEBUG` between them. The traced
build carries a scrolling program log in the corner of the screen:

```
1gam >turn
1gam who=0
2gam >sweep
2mus song=2
2dic >throw
2mus bed
```

A depth column, a three-letter subsystem, and a short message — so the log
can be filtered by eye before reaching for the filter key. `f1` turns the
overlay off, `f2` narrows it to one subsystem, `f3` prints the breadcrumb
(`gam>turn>sweep`) from a shadow return stack the program keeps as it
goes, and `f4` steps a line at a time. The sink is injected at boot rather
than compiled in, so a headless run can keep the breadcrumb and draw
nothing.

**It costs 3 297 bytes in the debug build and nothing at all in
production**: without the define, `dbg.c` is an empty translation unit and
every macro expands to `(void)0`. That is deliberate. A trace facility
that leaves a branch behind on a 1 MHz machine is a tax on every frame
forever, and the point of the migration was to stop paying taxes like
that.

## The thing the numbers do not say

Nearly every architectural decision in the BASIC edition exists *because* a
`POKE` costs 23 ms: paint only the gutters, split the plaque painter in
two, never repaint the whole screen, hoist every row address by hand,
declare the hot variables first so the interpreter finds them sooner, no
music during play, no cursor. All of them were correct. **None of them is
load-bearing any more.** That, rather than 194×, is what the migration is
for.

## Conformance

Speed that changes behaviour is a rewrite, not a migration. Two tests hold
the port to the original:

```sh
make conform    # both editions draw the opening board; the PNGs must be identical
make check      # the frozen edition's 13-row rule table, run on the host
```

`make conform` **no longer compares against the frozen edition.** It did,
byte for byte, until the board grew two rows for the black separators —
a fourteen-row board cannot be pixel-identical to a twelve-row one. What
it checks now is a golden snapshot of our own renderer, which answers
*has the board changed since somebody approved it* and not *does the
board still match the specification*. Re-baselining is `make
conform-bless`, deliberately a command you have to type on purpose, and
`make basicboard` still builds the frozen edition's board so the
comparison can be made by eye.

**That is a real loss and worth stating plainly**, because the old test
earned its keep on the first run: it caught cc65's
startup leaving the machine in the lower-case character set (where the
waiting ring and the home disc render as `w` and `q`) and the TED
background never being set to black (which matters because every engraved
element in the game is reverse video, and reverse video draws its glyph in
the *background* colour). Neither is a logic error; neither would have
been caught by a rule test. A reference written by a *different program*
catches what the author of this one did not think to check; a snapshot of
your own output cannot.

`make check` also runs 26 keyboard assertions — the eleven matrix
signatures, the debounce in both directions, buffering, and the idle
calibration — against `src/kbd.c` with its matrix read faked, and nothing
else faked. That substitution is only legitimate because the stage it
replaces was measured on real silicon by `src/kbhunt.c`; faking `$EF`
instead, which is what the KERNAL route's tests did, made every one of
them pass while no key could arrive at all.

`make check` compiles `src/rules.c` with the **system** compiler — it
contains no screen address, no TED register and no cc65 extension — and
runs the thirteen rule cases from `urroyal.bas` line 9600 in milliseconds
rather than a minute of emulation. 13 of 13 pass. Seven executor assertions —
the rosette flag and the capture count — are carried in the table but not
yet switched on; the executor exists, the assertions are backlog 4.5.

## Requirements

- [cc65](https://cc65.github.io/) — `brew install cc65` (has a first-class
  `plus4` target; `llvm-mos` does not)
- [VICE](https://vice-emu.sourceforge.io/) — `brew install vice`, for
  `xplus4`, `c1541` and the `petcat` used to build the BASIC half of the
  benchmark
- a C compiler for the host, for `make check`
- …or the real restored Plus/4 via the SD2IEC path in the repository's
  restoration docs

## Build & run

```sh
make            # build/urfinkel.prg
make music      # recompile tools/songs.mml -> src/song.h
make check      # rule tests on the host, milliseconds
make conform    # renderer conformance against the frozen edition
make bench      # both halves of the measurement, as screenshots
make test       # boot, let the attract start a demo, screenshot it
make run        # boot in the VICE xplus4 emulator
make run200     # ...at 200%, for watching a whole demo game
./start.sh      # shorthand for make run (works without brew on PATH)

make disk       # pack into build/urfinkel.d64
make card       # sync .prg and .d64 to the card's DEV folder AND its root
make card-eject # flush and unmount so the card can be pulled
make clean
```

On the machine, from the card root:

```
dload"urfinkel"
run
```

The gate for any change is `make check && make conform`.

`make debug` builds the traced binary; `make debug-shot` boots it headless
and screenshots the trace panel, which is the tool for a machine that has
stopped rather than one that is merely wrong.

## Layout

| Path | What it is |
|---|---|
| `src/plus4.h` | the machine: screen `$0C00`, colour `$0800`, TED registers, character sets |
| `src/blit.s` | `ca65`: one run of cells into both matrices, 25 cycles each |
| `src/irq.s` | `ca65`: the raster interrupt on `$FFFE` — two arpeggios, four times a frame |
| `src/board.c` | the renderer — table, buttons, tile painter, plaques, `glide`, the shimmer |
| `src/rules.c` | the Finkel ruleset, with no machine in it, so the host can test it |
| `src/text.c` | the writer and the chronicle |
| `src/dice.c` | randomness, the four lots, the tumble |
| `src/urbot.c` | four doctrines over one opportunity scan |
| `src/music.c` | the 50 Hz sequencer: chords, songs, sparkles, effects |
| `src/kbd.c` | the TED keyboard matrix — `$FD30`/`$FF08`, scanned once a frame |
| `src/front.c` | the cabinet: theatre front, lamp chase, curtains, the trophy |
| `src/etch.c` | the apron effects: the laser, the block font, the fire, the fireworks |
| `src/dbg.c` | the trace facility — an empty translation unit without `-DDEBUG` |
| `src/game.c` | the controller, the front end, the end of a match |
| `src/demo.c` | draws the opening board and parks — the conformance proof |
| `src/bench.c` | times the primitives against the jiffy clock |
| `test/test_rules.c` | the frozen edition's rule table, run by the host compiler |
| `test/test_kbd.c` | the keyboard decode and debounce, run by the host compiler |
| `src/kbdiag.c`, `src/kbhunt.c`, `src/kbtype.c` | keyboard instruments: the jiffy-clock verdict, the matrix signatures, the live probe |
| `tools/mml.py`, `tools/songs.mml` | the music notation and its compiler |
| `tools/viceshot.py` | headless screenshots that verify the program actually ran |
| `bench/*.bas` | the blocks appended to a throwaway copy of `urroyal.bas` for the BASIC half |

## Documentation

| File | Purpose |
|---|---|
| [docs/lab-report.md](docs/lab-report.md) | the migration report: baseline, why BASIC costs it, the chain, results, conformance |
| [docs/architecture.md](docs/architecture.md) | target design: modules, memory, blitter, animation, controller, status |
| [docs/keyboard-lab-report.md](docs/keyboard-lab-report.md) | reading a matrix with no operating system under it: the toolchain, the two faults, the hardware run |
| [docs/keyboard-report.md](docs/keyboard-report.md) | the fault trace behind it, instrument failures included |
| [docs/music.md](docs/music.md) | two voices, one volume, and what can be done with them |
| [docs/project-review.md](docs/project-review.md) | what the migration delivered, what it cost, and what is still wrong |
| [CONTRIBUTING.md](CONTRIBUTING.md) | forking, the signed-commit rule, the gate, and the two constraints that bite |

The reports cite `backlog.md`, the epic-by-epic plan. That is a working
document and is not published here; it stays in the development tree.

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Paul Richeson.

Written by **Paul Richeson** — [`vonglurt`](https://github.com/vonglurt) on
GitHub. Contact: paulr@sdf.org. The handle is the publishing name; the
copyright holder is the person.
