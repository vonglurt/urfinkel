# UR FINKEL — Architecture

How the compiled edition is put together, what it inherits unchanged from
the BASIC edition, and what it is free to do differently now that a screen
cell costs 60 µs instead of 23 ms.

The game is playable: four modes, the rules, the AI, the animation and the
music. Sections marked **(planned)** or **(not taken yet)** describe design
that is decided but not built. What exists today is listed in §9, and the
music has its own document, [`music.md`](music.md).

---

## 1. The inheritance

Three things carry over from the BASIC edition **unchanged**, because they
were right and because changing them would forfeit the conformance tests:

- **The path model.** `piece[player][1..7]`, where 0 is the pool, 1–14 is
  the board and 15 is home. Each player has a private 1-D path of 14
  squares; squares 5–12 map to the same physical cells for both players,
  which is where capture applies. This is the single simplification the
  whole game rests on and it has never needed to change shape.
- **The screen geometry.** Twenty 4-wide, 4-tall shaded buttons on a
  5-column stride, three bands of four rows, the figure-eight waist at
  columns 19–29 holding the two player plaques, the casting floor at rows
  12–20, the chronicle at 21–24. Every colour byte and screen code is the
  same number the BASIC source POKEd.
- **The rules.** The Finkel ruleset as the frozen edition's thirteen-row
  table defines it, plus its deterministic golden race.

One thing carries over as a *constraint on the port* rather than as code:
the renderer must produce a byte-identical screen (§7).

## 2. What changes, and why

The BASIC edition's architecture is a set of correct responses to a 23 ms
`POKE`. The migration lab report tabulates them
([`lab-report.md`](lab-report.md) §VIII); the short version is that the
budget went from about 1000 cell-writes per *match* to about 1000 cell
writes per *frame*, and the design question inverted from "how few cells
can I touch" to "what should the machine do with the time".

## 3. Module map

```
src/
├── plus4.h      the machine: screen $0C00, colour $0800, TED regs, charset
├── blit.s       ca65: the per-cell inner loop, 25 cycles/cell
├── irq.s        ca65: the raster interrupt - two arpeggios, 200 Hz
├── board.h/.c   the renderer: table, buttons, tile painter, plaques, glide
├── rules.h/.c   the Finkel ruleset - no machine in it, host-compilable
├── text.h/.c    the writer, and the chronicle
├── dice.h/.c    randomness, the four lots, the tumble
├── urbot.h/.c   four doctrines over one opportunity scan
├── music.h/.c   the 50 Hz sequencer: chords, songs, sparkles, effects
├── kbd.h/.c     the TED keyboard matrix, scanned once a frame
├── song.h       generated from tools/songs.mml (music.c only)
├── song_ids.h   generated: the song names, safe to include anywhere
├── game.c       the controller, the front end, the end of a match
├── demo.c       draws the opening board and parks (conformance proof)

test/
├── test_rules.c   the frozen edition's rule table, run by the host cc
└── test_kbd.c     the keyboard decode and debounce, run by the host cc

tools/
├── mml.py       the music notation compiler
├── songs.mml    the songs, as text
└── viceshot.py  headless screenshots that verify rather than hope
```

### 3.1 The host/target split is the load-bearing line

`rules.c` contains no screen address, no TED register and no cc65
extension. That is not tidiness — it is what lets the ruleset be tested by
the system compiler in milliseconds (`make check`) instead of a minute of
emulation. Everything that can live on that side of the line should:

| Belongs in the host-testable half | Belongs in the target half |
|---|---|
| move generation, move execution | anything writing to $0800/$0C00 |
| capture, rosette, bear-off, win detection | TED registers, IRQ, sound |
| URBOT's census, opportunity scan, doctrines | joystick, disk |
| the keyboard's *decode and debounce* | the keyboard's *matrix read* |
| the chronicle's *sentences* | the chronicle's *rendering* |

The rule is: **if it decides something, it is host-testable; if it shows
something, it is not.** The BASIC edition could not draw that line at all,
because everything was one file and one namespace.

## 4. Memory

```
$0800-$0BFF   colour matrix        (40x25, luminance*16 + hue)
$0C00-$0FFF   screen matrix        (40x25, screen codes)
$1001-$6E90   program: STARTUP, LOWCODE, CODE
$6E9F-$8260   RODATA              <-- crosses $8000
$8261-$86E3   DATA, INIT, BSS     <-- entirely above $8000
$86E4-$F500   free
$F500-$FD00   2 KB software stack, growing down from __HIMEM__
$D000/$D400   character ROM: upper-case/graphics, lower-case
$FF00-$FF1F   TED registers
```

Those four boundaries move with every build - they are this build's, from
`cl65 ... -Wl -m,map`, and the only one that is a design invariant is the
$0400 between the two matrices. Regenerate rather than trust them.

**Everything above $8000 is RAM only while the ROM is switched out**, and
that is not a footnote. Writes to $8000-$FFFF always land in RAM; reads
come from whichever is switched in with $FF3E/$FF3F. cc65's plus4 runtime
runs the program with RAM switched in, which is why the linker is willing
to place BSS at $8261 at all - but its interrupt stub switches the ROM
back **in** before chaining to $0314, so anything reached that way sees
BASIC ROM over its own variables. That cost this project weeks; see §8.3.

The whole compiled game - renderer, rules, AI, dice, text, music engine,
keyboard, controller, theatre front and trophy - is **56 511 bytes** on
disk: 30 119 of code and 26 106 of read-only data, most of that the
transcribed songs.  Against 25 681 for the BASIC edition tokenised the
compiled program is *larger*; what the speed bought was animation, a
music engine and a keyboard driver, not size.

There IS memory pressure, and it is now the governing constraint.  The
last link left **364 bytes** free.  Song data is capped by `MIDBUDGET` so
that the cost of a feature is paid in music rather than in an unexplained
link failure: the Makefile records the ceiling coming down from 23 400 to
22 200 on 2026-08-08, buying 1 525 bytes for the apron effects at the
price of three pieces out of the rotation.  The traced build (`make
debug`) links again. `-DDEBUG` costs **3 671 bytes** — 2 936 of code, 246 of
read-only data, 374 of BSS — against 324 free, so it was 3 347 short; the
figure recorded here before, ~1.6 KB, was wrong.

`MIDBUDGET` could not buy that back, because the `assets/midi` it transcribes
is not published here. What pays for it instead is
[`tools/bedtrim.py`](../tools/bedtrim.py): the traced build compiles the
first **`DBGBEDS`** transcribed beds (12 of 19 by default) rather than all of
them, and **the shipped game keeps every one**. The two binaries are
otherwise the same sources at the same flags, which is the property that
makes a trace worth reading.

**The $0400 invariant.** The colour matrix sits exactly $0400 below the
screen matrix. Every blitter leans on this — one pointer walks both
matrices, the colour pointer being the screen pointer minus $0400 (`SBC
#$04` on the high byte). Nothing in this project relocates the video
matrix through TED `$FF14`, and anything that wants to must fix the
blitter first. Double buffering (§6) is the one thing that would.

## 5. Rendering

### 5.1 `blit_run` — the one primitive

```
blit_ptr = <somewhere in the screen matrix>
blit_ch  = <screen code>
blit_cl  = <colour byte>
blit_run (n);          /* n cells, both matrices, one pass */
```

Parameters are globals rather than stack arguments deliberately: cc65's
stack calling convention costs more to unpack than this routine costs to
run. The inner loop is 25 cycles per cell.

Every drawing operation in this game is that shape — a button is four
runs, a gutter is one cell, a plaque row is one run, a screen clear is 25
runs — which is why there is exactly one primitive and not a family.

### 5.2 Row addresses

`rowtab[25]` holds the address of each screen row, built once by
`board_init`. This replaces the BASIC edition's hand-hoisting of row bases
into variables (`ZA=SC+800`), and it exists for a different reason: not
because multiplication is unfolded, but because a 6502 multiply by 40 is a
routine call and a table lookup is four cycles.

### 5.3 The tile painter

`paint_button` is a direct port of BASIC `7200` and keeps its exact
structure, including the single condition that distinguishes the two kinds
of square:

```
        ┌───┬───┬───┬───┐
        │ hl│ hl│ hl│ hl│   luminance +2 (capped at 7)
        ├───┼───┼───┼───┤   — but −2 on the bridge (y == 4)
        │ tl│ ▓ │ ▓ │ tl│   base rows; the two middle
        ├───┼───┼───┼───┤   columns are the well
        │ tl│ ▓ │ ▓ │ tl│
        ├───┼───┼───┼───┤
        │ sl│ sl│ sl│ sl│   luminance −3 (floored at 0)
        └───┴───┴───┴───┘
```

The bridge is lit from below — its top edge fades to black where the home
rows fade to white — so the shared corridor reads as a sunken lane rather
than another row of keys. `if (y == 4)`, one line, exactly as in BASIC.

### 5.4 Known headroom **(not taken yet)**

The board draw is 123.4 ms for ~2030 cell writes, or ~54 cycles per cell
against the blitter's 25-cycle floor. The gap is C-side overhead: cc65's
six-argument call into `paint_button`, and the gutter pass making 90
single-cell `blit_run(1)` calls that each pay full pointer setup for one
byte of work. Moving both into `blit.s` should approach the floor and take
the board draw toward ~50 ms. Not done yet — 123 ms is already 194× and
correctness comes first.

## 6. Animation

The BASIC edition has no double buffer because it could not afford one,
and compensates with strict per-region ownership so that only disturbed
cells are repainted. That discipline is worth keeping on its own merits;
what changes is that it is no longer the *only* option.

- **Localised repaint stays the default.** It is cheap, it is already
  written, and it is what makes the screen rock-steady.
- **Full-screen repaint becomes affordable.** A screen clear is ~18 ms;
  a whole board is 123 ms and falling. Anything that wants to redraw
  everything now may.
- **Real page flipping is available.** TED `$FF14` moves the video matrix,
  so two screens can be alternated in hardware. The cost is the $0400
  invariant (§4): a second buffer needs its colour matrix at the same
  relative offset, and the blitter needs to take a base rather than assume
  one. Worth doing only if a full-screen effect wants it; the board game
  probably does not.
- **Frame timing is on the raster.** `wait_frames` polls TED `$FF1D`,
  which is accurate and free where a calibrated `FOR` loop was neither.
  The TED register macros are `volatile` for a reason that cost an
  afternoon: with `-Osir` cc65 will hoist a non-volatile read out of a
  spin loop and then wait for a value that can never arrive.
- **Motion is tweened.** `glide()` walks the straight line between two
  character cells, saving and restoring each cell it covers. The frozen
  edition moved a piece one whole board *square* per step because a
  square was 32 POKEs; at 2 ms a square there is room for five steps
  where there was one. The dice tumble went from one-to-three frames to
  sixteen-to-twenty-one for the same reason.

### 6.1 The shimmer, and the arithmetic that makes it drift

`shimmer_frame` lifts the luminance of a moving crest across the tiles.
It went through four designs, and the last one is the only one that
actually drifts:

| | What it was | Why it went |
|---|---|---|
| 1 | a vertical band, left to right | a spotlight on a flat sheet, and looked like it |
| 2 | a chevron, `x + 2·|y − vein|` | better, still weather passing *over* the board |
| 3 | a point orbiting each tile's rim | read as something crawling round the edge, and the drift broke at every tile boundary |
| 4 | a **vertical pair drifting along the face** | current |

The glint is now a tile's **highlight cell and its shadow cell in the same
column**, moving horizontally together, so it reads as a sheen crossing the
face. The two **outer bands run left and the middle band runs right** — two
currents passing one another, which is also what the board means: the outer
rows are each player's own track, the middle is the corridor they contest.

**The phase multiplier is arithmetic, not taste.** A tile is 4 cells wide
on a 5-column pitch and the crest moves one cell per step, so for the light
to run on unbroken into the next tile that tile must begin **five steps
later** — and in an eight-step ring, "five later" is "three earlier". Hence
`ci * 3` rightward and `ci * 5` leftward. Get it wrong and the crest jumps a
gap or doubles back at every boundary, which is exactly what design 3 did.

Two invariants it must not break:

- **Colour RAM only.** The screen matrix never changes, so the shimmer can
  never disturb a glyph; the worst it can do is get a colour wrong for one
  frame.
- **Never the middle two rows of a tile.** They carry the well, and the well
  carries pieces, rosette stars and the occupant's flood colour. A glint
  crossing them would repaint a piece's own colour out from under it twice
  a second.

It also only ever adds luminance, and skips a dimmed band entirely — so the
shimmer is simultaneously decoration and the answer to *whose turn is it*.

## 7. Conformance

Two tests, and they check different things.

### 7.1 The screen, as an image

`make conform` builds both editions drawing the opening board headless and
compares the PNGs with `cmp`. They are currently byte-identical. **This is
the acceptance test for every renderer change**: the compiled port is
allowed to be faster, not different.

It is worth more than it looks. The first compiled board differed in two
ways that no rule test could have found and no code review did: cc65's
startup leaves the machine in the lower-case character set (where the
waiting ring, the home disc and the rosette star are the letters `w`, `q`
and `*`), and the TED background register was never set to black — which
matters because every engraved element in this game is reverse video, and
reverse video renders its glyph in the *background* colour.

### 7.2 The rules, as a table

`make check` runs the frozen edition's thirteen-row rule table
(`urroyal.bas` line 9600) against `rules.c` on the host. Thirteen of
thirteen generator checks pass; seven executor assertions are carried in
the table unasserted and counted, waiting for the move executor.

**The on-target self-test is not replaced by this.** The host test is the
fast inner loop. The machine stays the final word, because only the
machine exercises the machine — and the golden race (28 throws for *four*,
6 for *zero*, 7–0) is the end-to-end statement that the two editions play
the same game.

## 8. The controller

The shape does not change:

```
   set turn colours → wipe chronicle/floor → throw → legal moves →
   choose piece (human or AI) → execute + animate → win/rosette →
   switch player → repeat
```

What changed is that **nothing blocks**. Every wait in `game.c` goes
through `poll_key`, which services the music sequencer and stirs the
random state on the way past, so the ambient bed keeps playing and the
dice keep being unpredictable while the game sits waiting for a human to
choose a piece. The predecessor had to choose between waiting and doing,
and documented the "cooperative sequencer" as an icebox item for exactly
that reason.

- a raster interrupt drives the music and a frame counter (§6, `irq.s`);
- the controller polls and never blocks on input;
- randomness is stirred from the raster position at the moment of a
  keypress - which changes 312 times a frame rather than once, and is a
  finer-grained version of the BASIC edition's attract-loop entropy.

### 8.1 Two traps this target sets for C

Both are invisible to the compiler, and both cost real time to find:

1. **Never call C from the interrupt, and never borrow its scratch.**
   cc65 compiles to a software stack with shared zero-page temporaries,
   so an interrupt that enters C - or merely uses `tmp1` for one byte -
   corrupts whatever the main line was in the middle of. `irq.s` keeps
   its own byte in `.bss`.
2. **Never call the KERNAL jump table directly.** cc65's plus4 runtime
   keeps the ROM banked out except around its own wrappers, so `jsr $FFE4`
   executes whatever RAM lies under it. The symptom was eleven characters
   missing from one menu row, which looks exactly like a renderer bug and
   is not. The character-set switch goes through `cbm_k_bsout`, and it
   is the program's only KERNAL call.
3. **An interrupt handler on `$0314` cannot read its own variables.** The
   same banking fact met from the other side, and the most expensive
   defect in the project. cc65's plus4 runtime puts its own stub on the
   RAM `$FFFE`, and that stub switches the ROM back **in** before it
   reaches `$0314` - so a handler there is entered with BASIC ROM over
   `$8000-$BFFF`. cc65 puts BSS at the end of the program, and once the
   binary passed ~30 KB `irq.s`'s variables landed above `$8000`: every
   one of them was read from ROM and written to the RAM underneath.
   `irq.s` now takes `$FFFE` directly and is never entered through the
   KERNAL. Full trace in [`lab-report.md`](lab-report.md) §IX‑D5.
4. **The KERNAL's periodic interrupt does not run under this runtime at
   all**, so `$EF` is never filled and `conio`'s `kbhit()` can never
   return true. `kbd.c` reads TED's matrix through `$FD30`/`$FF08`
   instead and owes the KERNAL nothing. See
   [`keyboard-lab-report.md`](keyboard-lab-report.md).

Not a toolchain trap, but the same class of thing and it cost as much:

5. **A debounce counted in calls is not a debounce.** `kbd_get` once
   scanned and debounced together, so the rate was set by whichever loop
   called it - microseconds apart in a bare spin, a third of a second
   apart in the menu. `kbd_scan` now runs once a frame from
   `wait_frames_live` and nowhere else, which makes the rate a property
   of the system rather than of the caller.

## 9. Status

| Component | BASIC routine | Compiled | State |
|---|---|---|---|
| per-cell blitter | inline POKEs | `blit.s` | **done** |
| row-address table | `ZA=SC+…` by hand | `board.c` | **done** |
| tile painter | `7200` | `paint_button` | **done** |
| board table (gutters) | `1200` | `paint_table` | **done** |
| the twenty buttons | `1220` | `paint_buttons` | **done** |
| waist plaques | `7700`–`7799` | `paint_plaques` | **done** |
| token rows | `7650` | `paint_tokens` | **done** |
| chronicle reset | `8050` | `chronicle_reset` | **done** |
| legal-move generator | `3500` | `find_legal_moves` | **done** |
| geometry tables CX/CY/CC | `130`–`200` | `geometry_init` | **done** |
| draw one board cell | `7000` | `draw_cell` | **done** |
| move executor | `5000` | `execute_move` | **done** |
| the chronicle | `8000` | `say`, `text.c` | **done** |
| turn controller | `2000`–`2210` | `play` | **done** |
| dice + tumble | `3400`–`3495` | `throw_lots` | **done** |
| motion tweening | *(unaffordable)* | `glide` | **done** |
| URBOT | `4500`–`4684` | `urbot.c` | **done** |
| colour picker | `2600`–`2705` | `colour_pick` | **done** |
| menu + attract | `400`–`440` | `menu` | **done** |
| rules screen | `9000`–`9205` | `rules_screen` | **done** |
| victory | `6000`–`6110` | `victory` | **done** |
| two-voice music | `8800`–`8950` | `music.c`, `irq.s` | **done** |
| songs as text | *(inline `DATA`)* | `tools/mml.py` | **done** |
| music during play | *(out of scope)* | the ambient bed | **done** |
| victory trophy | `6200`–`6285` | `trophy_draw`, `front.c` | **done** |
| theatre front + curtains | `8400`–`8580`, `8700` | `front.c` | **done** |
| lamp chase | `8500` | `lamps_frame` | **done** |
| tile shading profile | *(four constants)* | `shade_profile` | **done** |
| the shimmer | *(unaffordable)* | `shimmer_frame` | **done** |
| whose turn it is, in light | *(unaffordable)* | `player_dim`, `board_relight` | **done** |
| the travelling flash | *(unaffordable)* | `square_tint`, `game.c` | **done** |
| trace facility | *(unaffordable)* | `dbg.c`, `-DDEBUG` only | **done** |
| keyboard | `GET`/`GETKEY` (KERNAL buffer) | `kbd.c` (TED matrix) | **done** |
| on-target self-test | `2500`, `9300` | — | planned |

The ordered plan is in `backlog.md`, a working document held in the
development tree rather than published here.

## 10. Build chain

```
   assets/midi/*.mid ── midibed.py ──┐
                        transcribe   │
                        + pack to    ├─► tools/songs-midi.mml ─┐
                        a budget     │                         │
   tools/songs.mml ───────────────────────── the cues ─────────┤
      (hand-written)                                           │
                                                     mml.py ───┴─► src/song.h
                                                                 + song_ids.h
                                                                 + song_beds.h
   src/*.c ──── cl65 -t plus4 -Osir -Cl ────► build/*.prg
      │                                            │
      │                                     c1541 ─┴─► .d64 ──► xplus4
      ├──── cc -std=c99 (host) ──► make check              └──► SD2IEC ──► Plus/4
      │        rules + keyboard decode, milliseconds
      │
   src/blit.s ── ca65 ──► the per-cell inner loop
```

`-Osir` is cc65's full optimiser; `-Cl` makes locals static rather than
stack-allocated, which matters because the 6502's software stack is
expensive to index. Together they are worth about 20% — see
[`lab-report.md`](lab-report.md) §VI‑A, and note that they are *not* where
the two orders of magnitude came from.

Targets: `make` builds the game, `make debug` builds the same sources
with `-DDEBUG` for the traced binary, `make check` tests the rules on the
host, `make conform` diffs the rendered board against the frozen edition,
`make test` boots the
game and screenshots a demo game in progress, and `make run` boots it in
VICE.

**`make music`** rebuilds the whole song table. It transcribes every
`assets/midi/*.mid` and packs in as many as `MIDBUDGET` allows, then
compiles those together with the hand-written cues in `tools/songs.mml`.
Anything that will not fit is **named in the build log with its size** —
adding a `.mid` can never silently overflow the machine. See
[`music.md`](music.md) §4.

**`make music-budget`** prints what the last link actually left free, which
is the only honest input to `MIDBUDGET`. Raise the budget only after
re-running it; the margin at the time of writing is 557 bytes.

**`make card`** deploys the `.prg` and the `.d64` to the SD2IEC card's
root *and* its `dev/` folder, *and* to a dated snapshot folder
`dev/<yymmddhhmmss>/` so the card carries a history rather than only the
newest build. The stamp comes from the `.prg`'s mtime rather than from
`date`, so re-syncing one binary refills one folder instead of littering
three. Every copy is `cmp`-verified before the target reports success.
`make card-eject` unmounts.

The gate for any change is `make check && make conform`.

---

*Released under the MIT License. Copyright (c) 2026 Paul Richeson.*
