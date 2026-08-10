# UR FINKEL — Backlog

The ordered plan for taking UR ROYAL from interpreted BASIC 3.5 to
compiled 6502. `[x]` = delivered.

The ordering principle is **conformance before features and features
before speed**. The frozen BASIC edition already plays a correct, complete
game, so this project has an oracle for everything it does; the fastest
way to get it wrong is to optimise something before there is a test that
says what it should do. The one exception is epic 1, because a migration
that cannot state its baseline cannot claim anything.

Background, measurements and the reasoning behind the chain are in
[`docs/lab-report.md`](docs/lab-report.md). The target design is in
[`docs/architecture.md`](docs/architecture.md). What the migration
delivered, cost and got wrong is in
[`docs/project-review.md`](docs/project-review.md).

---

## Where this stands

`make check` **green** (13/13 rules + 26/26 keyboard) · `make conform`
**green** (byte-identical) · `make test` **green** — and see 16.8 for the
two different reasons it had been red, only one of which was the game.

**It runs on the real machine.** 2026-08-07, loaded from the SD2IEC onto
the restored PAL Plus/4: the game boots, the keyboard answers, and a match
plays. First hardware validation of any kind (13.1); every other number in
this project is from emulation.

**The keyboard is fixed — it took two fixes, and the first hid the
second.** The KERNAL's interrupt does not run under cc65's plus4 runtime
at all, so `$EF` was never filled and `kbhit()` could never return true;
`make kbdiag` establishes that from the host without pressing a key, by
watching the KERNAL's jiffy clock rather than its keyboard buffer.
`src/kbd.c` reads TED's matrix directly instead. That still did not make
the menu answer: the debounce was counted in **calls** rather than in
time, so a bare spin scanned thousands of times a second and debounced
nothing while the menu scanned once every third of a second and missed
every tap. `kbd_scan` now runs once a frame from `wait_frames_live`.
Reports: [`docs/keyboard-lab-report.md`](docs/keyboard-lab-report.md) and
[`docs/keyboard-report.md`](docs/keyboard-report.md). §C below is answered
rather than open.

The game plays: a theatre front with a fanfare curtain, four modes, the
full Finkel ruleset, URBOT narrating itself, a generated ambient bed, a
gold trophy, and a board that says whose turn it is in light. Production
is **29 374 bytes** against the BASIC edition's 25 681 — it has passed the
predecessor, and the TED keyboard scanner, the sixteen-step figure, four
written beds and the walking lots are what bought that.

**The machine is FULL, and that is now a fact to plan against rather than
a surprise.** The transcribed beds are sized to fill whatever the code
leaves (`MIDBUDGET`), so every byte the program grows comes out of the
music, and `make music-budget` is the only honest reading of it. Two
things follow, both found on 2026-08-08 while giving the capture and the
win the whole apron (7.6):

- The apron effects cost **1 525 bytes** of code and .bss, 240 of which is
  the firework trail alone. Paid for by `MIDBUDGET` 23 400 → 22 200, which
  drops three pieces from the bed rotation — 21 down to 19 — and leaves
  **364 bytes free** where there had been 545. Nothing ancient was
  dropped: seikilos and both Hurrian settings are still in. **364 bytes is
  not much of a margin** and the next feature of any size has to arrive
  with its own `MIDBUDGET` cut; that is the arrangement, not a surprise.
- **`make debug` does not link, and had not for some time before this.**
  Test row A3 says both builds link and it has been red without anyone
  noticing: the traced build wants ~1.1 KB more than exists, ~1.6 KB now.
  It is not a debug-build fault, it is the music budget — see 15.18.

**The next five things, in the order they should happen:**

1. **16.6 — make the demo deterministic.** Everything else in epic 16 is
   blocked on it, because until two builds play the same match, nothing
   about the wedge can be compared between them. Still first, and now for
   a sharper reason: 16.10 removed a real defect and the wedge has not
   been seen since, which is exactly the evidence a fixed seed is needed
   to tell apart from luck.
2. **16.1–16.8 — the wedge.** The banking fault behind the frozen
   interrupt is found and fixed (16.10); whether it was also the wedge is
   open (16.11).
3. **3.11 — conformance on a mid-match position.** Every renderer change
   is riskier than it needs to be while the oracle only covers the empty
   board, and this project has now made several.
4. **4.5 — the seven deferred executor assertions**, so the deferred
   count reaches zero.
5. **Epic 10 — the golden race on-target.** 28 throws for *four*, 6 for
   *zero*, 7–0. It is the strongest available statement that the two
   editions play the same game.

Everything after that is features (epic 14), speed (epic 12, deliberately
last) and hardware (epic 13).

---

## Test procedure

Two halves, because neither of us can see what the other can. The host
can run the ruleset thousands of times a second and diff screenshots to
the byte; it cannot press a key, hear a note, or tell you what a real
Plus/4 does with a raster interrupt. The bench can do all of those and
none of the first. Every row below says which half owns it.

### A. The host half — run before anything is called done

| | Command | Passes when |
|---|---|---|
| A1 | `make check` | 13/13 generator checks. Milliseconds; run it constantly |
| A2 | `make conform` | `conform ok` — the compiled board byte-identical to the frozen edition's |
| A3 | `make` and `make debug` | Both link. The production size should not move much; the traced one is ~3 KB larger |
| A4 | `make music` | The mml compiler reports the songs, the bed and the pairing period |
| A5 | `make test` | **Green.** The attract timeout starts the demo and it plays a match without stopping. Judged on `--max-white`, not on black — see 16.8 |

A2 is the load-bearing one. If a change was not meant to alter a pixel
and A2 fails, the change is wrong — do not re-baseline the screenshot to
make it pass.

- [ ] A6 Wire A1–A4 into one `make gate` target, so there is a single
      thing to run rather than four to remember

### B. The bench half — only the machine can answer these

Load from the card: `dload"urfinkel"` then `run`.

- [ ] B1 **Boot.** The theatre front draws, the marquee lamps chase, the
      curtains open on the fanfare. Failure here is a load or a charset
      problem, not a game problem
- [ ] B2 **The cabinet is alive.** Watch the lamps for ten seconds. They
      must keep moving. *If they stop, stop testing and record it — that
      is epic 16 and nothing below it means anything*
- [ ] B3 **Sound.** The fanfare on the curtain, then the ambient bed
      underneath the menu. Two voices, quiet
- [ ] B4 **A key that does not change the screen: press `m`.** The music
      should stop; press again and it returns. This isolates the key path
      from everything else — no screen change, no mode change, just the
      read
- [ ] B5 **A key that changes the screen: press `5`.** The rules screen,
      with the path numbered 1→14. Space returns
- [ ] B6 **A key that starts a game: press `1`.** The curtains close —
      about two and a half seconds — and *then* `player one, your name:`
      appears. **Give it three seconds before deciding nothing happened**
- [ ] B7 **Type a name and press return.** Then the colour picker: hue
      1–16 and shade **3–6** only. Anything outside that range must be
      refused with a buzz
- [ ] B8 **Play a turn.** The lots tumble for about a second and a half
      and settle one at a time; the chronicle names the count; the piece
      travels cell by cell with a flash running ahead of it
- [ ] B9 **The board says whose turn it is.** The waiting side's four
      rows and its plaque are visibly darker, and the shimmer crosses
      only the active side
- [ ] B10 **A capture, when one happens.** Two effects, one after the
      other, and they are separate things. First the SQUARE goes red,
      white, red, black and then the winner's colour — different from an
      ordinary landing, which is white then the mover's colour. Then the
      whole bottom of the screen catches fire for about four seconds: a
      bed of flame along the last six rows, `captured` in block letters
      standing over it and burning from its feet upward, and the log
      underneath it gone. **The log must come back** when the fire goes
      out — that is `apron_clear`, and it is the part most likely to be
      wrong
- [ ] B11 **Leave it alone for two minutes at the menu.** The demo starts
      by itself. This one needs no keyboard at all, which is what makes
      it the cleanest proof the program is still running
- [ ] B12 **A whole match to a win.** Border flare, the theme, the
      winner's name cut across the whole width in block letters, then the
      gold cup poured a row at a time with the name engraved into it, then
      a firework display — several shells in the air at once, and the
      border flashing each shell's colour as it bursts — then `play
      again?`. About twelve seconds end to end. **Watch the border**: it
      must come back to the winner's colour when the last shell dies, not
      stay stuck on a firework's hue

### C. The keyboard question, specifically

**Answered — see the verdict below.** The reasoning is left standing
because the way it was wrong is the useful part. A key reaches the program
in two stages: the KERNAL's interrupt scans the matrix and bumps the count at
`$EF`, then `kbhit()` reads it. **Stage 2 is proven** — VICE's `-keybuf`
writes `$EF` directly and the menu responds correctly at every delay
tried. **Stage 1 cannot be tested from the host at all**, because writing
`$EF` by hand is precisely what jumps over it. And UR FINKEL took over the
interrupt vector at `$0314` for the music, passing non-raster interrupts
on to the old handler — if that hand-off fails, the KERNAL never scans,
`$EF` never moves, and no key is ever seen. *(It hangs off `$FFFE` now,
and for a reason nobody suspected when this was written: see 16.10.)*

`make kbtest && make card-probe`, then on the machine:

```
open15,8,15,"cd:dev":close15 : dload"kbtest" : run
```

It switches the raster interrupt on and off by itself every four seconds
— nothing in it is driven by the keyboard, because the keyboard is what
is on trial. Hold a key down and read the numbers:

**ANSWERED — it was C5, and it was answered from the host.** The claim
above that stage 1 "cannot be tested from the host at all" was true of
every test that works by *pressing a key*, and false in general. The
KERNAL's interrupt does two jobs: it scans the matrix AND it advances the
jiffy clock at `$A3`-`$A5`. The clock needs no keystroke to observe. `make
kbdiag` measures it frozen in all three bytes with our raster interrupt
installed **and** removed, while our own handler counts a clean 100 frames.

- C3 — no.
- **C4 — refuted.** Our raster interrupt is not starving anything; the
  clock is equally frozen with it removed. Nothing in `irq.s` was ever
  wrong, and the time this section would have sent someone to spend on
  `handler`'s `old_irq` chain would have been spent on the wrong file.
- **C5 — confirmed.** The KERNAL's interrupt does not run under cc65's
  plus4 runtime at all, so `$EF` is never filled and `kbhit()` can never
  return true.

**Fixed by routing around it, not by repairing it.** TED scans this
machine's keyboard itself; `src/kbd.c` reads the matrix through
`$FD30`/`$FF08` and `poll_key` calls it instead of `conio`. The protocol
was confirmed with `make kbhunt-run` against six keys, all six matching
the documented matrix. Full trace, hierarchy and measurements in
[`docs/keyboard-report.md`](docs/keyboard-report.md).

Still open, and cheap:

- [x] C6 **Column 7 is fine.** `make kbdiag-shot` prints the idle matrix
      and all eight rows read 255, so nothing is stuck and `kbd_init`'s
      calibration masks nothing — `SHIFT X V N , /` are all reachable. The
      earlier stuck-low reading was a probe that strobed `$FF08` without
      selecting a row through `$FD30` first
- [ ] C7 SHIFT and auto-repeat, if name entry wants upper case
- [ ] C8 `dbg.c`'s `f4` step mode still calls `cgetc()` and would block
      forever in the traced build

---

## Epic 0 — Fork the project and freeze the predecessor

- [x] 0.1 Rename `src/urroyal/` to `src/urroyal-basic/`, so the two
      editions are sibling directories rather than one shadowing the other
- [x] 0.2 `MAINTENANCE.md` in the frozen edition: what changes are still
      accepted (crash, wrong-rule, toolchain, documentation), what are
      not (features, performance, layout), and the rule that guards it —
      no accepted change may move the golden race
- [x] 0.3 Freeze banners on the frozen edition's `README.md` and
      `index.md`, pointing at this project
- [x] 0.4 Fix every path reference the rename broke: `.gitignore`, the
      repository's `docs/05-sd2iec-primer.md`, the frozen `index.md` and
      lab report
- [x] 0.5 Tag the delivered BASIC state `urroyal-basic-v1`, so the
      maintenance policy has a provenance line to point at
- [x] 0.6 Create `src/urfinkel/` — the compiled edition, named for the
      ruleset it implements

## Epic 1 — Measure before changing anything

- [x] 1.1 Benchmark harness for the frozen edition: patch a throwaway copy
      of `urroyal.bas` so line 300 jumps into an appended timing block,
      build with `petcat`, run headless under warp, screenshot the result.
      The frozen source is never modified
- [x] 1.2 Time five primitives against the jiffy clock: the board draw
      (`1000`), one 4×4 button (`7200`), both token rows (`7650`), one
      chronicle line (`8000`), and the legal-move scan (`3500`)
- [x] 1.3 Record the profile. Board draw 23 960 ms, button 594 ms, token
      rows 1 826 ms, chronicle 404 ms — **and the legal-move scan at
      2 785 ms**, the most expensive thing in a turn and not a renderer
      at all
- [x] 1.4 Compose the turn cost: ~12 s per turn, of which a quarter is
      spent deciding which moves are legal
- [x] 1.5 Matching benchmark on the compiled side, same clock, same
      operations, enough repetitions to be several jiffies long

## Epic 2 — The development chain

- [x] 2.1 Establish that `llvm-mos` has no Plus/4 platform, so the best
      6502 codegen available is not available here
- [x] 2.2 `brew install cc65`; confirm `plus4.cfg` and `plus4.lib`
- [x] 2.3 `cl65 -t plus4 -Osir -Cl` as the target build; measure what the
      flags are worth (151 ms → 123 ms on the board draw, ~20%)
- [x] 2.4 Host build of the rules module with the system compiler, so the
      ruleset can be tested in milliseconds
- [x] 2.5 `Makefile`: `all`, `check`, `conform`, `bench`, `run`, `disk`,
      `card`, `card-eject`, `clean`
- [x] 2.6 `run.sh` launcher, brew-safe PATH, matching the frozen edition's
- [x] 2.7 CI-shaped one-liner: `make check && make conform` as the gate
      any change has to pass
- [x] 2.8 A screenshot harness that verifies rather than hopes. VICE's
      autostart loses the RUN keystroke about one run in eight and no
      setting makes it certain, so `tools/viceshot.py` checks that the
      program actually ran - every program here ends with a black TED
      background, and the BASIC boot screen has none - and retries
- [x] 2.9 Deploy: `make card` syncs the `.prg` and the `.d64` to both the
      card's DEV folder and its root, each verified with `cmp`

## Epic 3 — The renderer, to pixel parity

- [x] 3.1 `blit.s`: one horizontal run of identical cells into both
      matrices, colour pointer derived from the screen pointer by the
      $0400 invariant. 25 cycles per cell
- [x] 3.2 Row-address table built once, so no drawing loop multiplies by 40
- [x] 3.3 Tile painter (`7200`): 4×4 shaded button, highlight over two
      base rows over shadow, the two-column well, the bridge's inverted
      top edge
- [x] 3.4 Board table (`1200`) — gutters only — and the twenty buttons
      (`1220`)
- [x] 3.5 Waist plaques (`7700`–`7799`): engraved nameplate, the dark
      margin at the bridge, and the tri-state token row
- [x] 3.6 Character set: ask for upper-case/graphics, where the ring, the
      disc and the reverse star live. cc65's startup selects lower case,
      where those codes are `w`, `q` and `*`
- [x] 3.7 TED background to true black, because every engraved element is
      reverse video and reverse video draws its glyph in the background
      colour
- [x] 3.8 **Conformance test**: both editions draw the opening board
      headless, screenshots compared with `cmp`. Byte-identical
- [x] 3.9 Geometry tables `CX/CY/CC` (BASIC `130`–`200`): path square to
      screen cell and base colour, per player
- [x] 3.10 Draw one board cell with its occupants (`7000`) — the
      foundation every animation cleanup stands on
- [ ] 3.11 Extend the conformance test to a mid-match position, so
      occupied squares, the flood-the-base-rows treatment and the piece
      digits are covered as well as the empty board

## Epic 4 — The rules engine, to specification parity

- [x] 4.1 Path model as bytes: `piece[2][8]`, 0 pool, 1–14 board, 15 home
- [x] 4.2 Legal-move generator (`3500`): own-piece blocking, the central
      rosette barred while a foe holds it, no overshoot
- [x] 4.3 Host test carrying the frozen edition's thirteen-row rule table
      verbatim; 13/13 generator checks pass in milliseconds
- [x] 4.4 Move executor (`5000`): capture in the shared corridor, rosette
      extra throw, bear-off on the exact count, win at seven home
- [ ] 4.5 Turn the seven deferred assertions in the host test on — the
      rosette flag and capture count — so the deferred count reaches zero
- [x] 4.6 The four tetrahedral lots as four binary draws summed
      (`3400`), which is the correct binomial 0–4 and not a uniform
      `RND*5`
- [x] 4.7 Opening throw: higher hand opens, ties re-throw (`715`–`736`)

## Epic 5 — The controller

- [x] 5.1 Turn loop (`2000`–`2210`) as a state machine rather than a
      blocking sequence: throw → legal moves → choose → execute →
      rosette/win → switch
- [x] 5.2 The four production modes, dispatched exactly as the frozen
      edition does: vs URBOT, two players, manual dice, URBOT demo
- [x] 5.3 Human piece selection with the destination list (`4000`), and
      the invalid-choice buzz
- [x] 5.4 Manual tip-count entry, 0–4 only (`3200`)
- [x] 5.5 Demo exits to the menu on space, polled at loop level only —
      the BASIC edition's reason (stranded `GOSUB` returns) is gone, but
      the discipline is still the right shape for a state machine
- [x] 5.6 Victory sequence: border flare, the winner named, the loser's
      pieces-home count, and the theme (`6000`–`6110`)
- [x] 5.7 The gold trophy painted over the casting floor with the winner's
      name engraved in the cup (`6200`–`6285`), and the play-again prompt.
      Poured a row at a time and engraved a letter at a time, because at
      compiled speed the whole cup would otherwise appear between two
      frames — see epic 15

## Epic 6 — Text and the chronicle

- [x] 6.1 A text writer: PETSCII to screen codes, reverse video, centred
      and left-justified, colour per line
- [x] 6.2 The chronicle (`8000`): four scrolling lines, oldest dimmest,
      one routine and one place for every word the program says
- [x] 6.3 Number and string formatting without `printf` — the destination
      lists, counts and throw totals the chronicle carries
- [ ] 6.4 Benchmark it against the frozen edition's 404 ms per line, which
      is the one primitive in the profile with no compiled counterpart yet

## Epic 7 — Dice and animation

- [x] 7.1 The four pyramid lots drawn as tetrahedra (`7900`): bone base,
      PETSCII diagonals, tipped or blank apex
- [x] 7.2 The tumble in absolute floor coordinates with rejection sampling
      so no two lots overlap, settling one at a time (`3400`–`3495`)
- [x] 7.3 Piece movement: draw on each intermediate square, restore the
      cell behind it from the base-colour table (`5075`–`5130`)
- [x] 7.4 Replace the inherited calibrated-delay loops with raster timing
      against TED `$FF1D`/`$FF1C`
- [x] 7.5 Turn-transition colour sweep (`8600`)
- [x] 7.6 **The two big moments take the apron.** A capture and a win were
      a word cut into one row of the eleven below the board, for two
      seconds each, while the dice — which happen every turn — had the
      other ten rows and one and a half. Both now own rows 14–24, the
      casting floor *and* the chronicle, which is affordable because the
      log repaints itself from its own buffer (`apron_clear`). A capture
      is 190 frames of fire; the winner's name is cut in block letters at
      200 and the display over the cup runs 250
- [x] 7.7 **A block font, four by five at a five column pitch.** Eight
      letters is thirty-nine of the forty columns — and eight is the
      length of a player's name, which is the whole reason for the size.
      The winner's name is cut at it and `captured` is burnt at it
- [x] 7.8 **Fireworks as a display rather than a demonstration.** Three
      shells in the air at once instead of four strictly one after
      another, a new one every eleven frames, twenty-four columns across
      the burst instead of ten, and the border flashing the shell's own
      hue for two frames — the only way a forty column screen can light
      the room up
- [ ] 7.9 The `home` marquee is still the small one, deliberately (it
      happens up to fourteen times a match). If bearing off ever wants a
      moment of its own it should be a *different* effect, not a longer
      one — a piece leaving the board rather than a word

## Epic 8 — URBOT

- [x] 8.1 Census and the single opportunity scan into five registers:
      bear-off, capture, rosette, cheapest entry, furthest runner
- [x] 8.2 The four doctrines as priority orders over those registers
- [x] 8.3 The re-draw once three pieces are home, latched so it happens
      once per match
- [x] 8.4 The random fallback, so URBOT can never stall
- [x] 8.5 Narration into the chronicle, branch by branch — the whole point
      of the AI is that a watcher can follow the decision tree
- [ ] 8.6 Host tests for the doctrines: given a board and a throw, assert
      which register fires. The BASIC edition could not test this at all

## Epic 9 — The front end

- [x] 9.1 Theatre front: frieze bands, banner, zigzags, curtains, floor,
      engraved credit (`8400`–`8495`). `src/front.c`
- [x] 9.2 Lamp chase, colour-RAM only, 2 lit of every 5, circulating
      (`8500`). Three rails; the middle one runs the other way
- [x] 9.3 Mode menu with the two-minute attract timeout into the demo
- [x] 9.4 Curtain-open sweep (`8700`), and a close to match. The BASIC
      curtain slid six columns because seventeen rows of POKEs a column
      was already most of a second; this one travels fourteen, from the
      middle of the screen out to the wings, in the 168 frames the fanfare
      lasts. Opening takes a `dress` callback and repaints the stage
      under the fabric each step, because there is nowhere on this machine
      to keep a copy of what a curtain is hiding
- [x] 9.5 Colour picker: sixteen named swatches in their own hue, then a
      live preview drawn by the real renderers, then keep-or-repick
      (`2600`–`2705`)
- [x] 9.6 Rules screen with the path numbered 1→14 in the wells
      (`9000`–`9205`)
- [x] 9.7 Entropy: the BASIC edition seeds `RND` from human timing in the
      attract loop. A compiled program has better options — TED raster
      position at the moment of the keypress, plus the jiffy clock — and
      needs its own generator anyway, since there is no `RND` to seed

## Epic 10 — The game tests itself, on the machine

- [ ] 10.1 Port mode 6's unit-check engine so the same table runs
      on-target as well as on the host
- [ ] 10.2 Port the scripted race: *zero* throws nil every turn, *four*
      throws a greedy exploding four, both through the ordinary controller
- [ ] 10.3 **The golden race must reproduce exactly**: 28 throws for
      *four*, 6 for *zero*, 7–0. This is the strongest statement available
      that the two editions play the same game, and it is deterministic
      because there is no randomness anywhere in its path
- [ ] 10.4 Verdict screen and a headless screenshot target, matching the
      frozen edition's `make ruletest-shot`
- [ ] 10.5 Error trapping equivalent to `TRAP 6900`: a crash should report
      where it was, not drop to a half-drawn board

## Epic 11 — Music

- [x] 11.1 Two-voice player over TED's two tone channels, melody plus
      harmony in scale-correct thirds and sixths
- [x] 11.2 The theme as data: D Dorian A phrase, Hijaz B phrase, raised
      third at the finish; pitch values from `N = 1024 − 110841/Hz` (PAL)
- [x] 11.3 **Raster-IRQ sequencer.** The frozen edition documents
      background music during play as out of scope because steady playback
      "needs a machine-language IRQ player (`SYS`), which is out of scope
      for the pure-BASIC constraint". That constraint is gone. Music
      during play, with effects sharing voice 2 by priority
- [x] 11.4 Fanfare on curtain-open and the per-player turn cheers, sharing
      the theme's pitch table
- [x] 11.5 A mute toggle, since music during play is a preference
- [x] 11.6 **Songs as text.** `tools/songs.mml` compiled by `tools/mml.py`
      into `src/song.h`: notes, octaves, accidentals, per-note lengths in
      ticks, two voices. `make music` is the whole authoring loop
- [x] 11.7 **The generated ambient bed.** A drone on voice 2, an
      arpeggiated triad on voice 1, the root walking the circle of fifths
      every three seconds, Fibonacci-spaced sparkles with decaying
      echoes, and the global volume modulated as a hurdy-gurdy buzz.
      Arpeggio 3x8 ticks against buzz 5x13 - adjacent Fibonacci numbers,
      so the two periods do not realign for eight seconds
- [x] 11.10 **Bank rates as notation.** `rate primary 1` / `rate second 3`
      in `songs.mml`. The compiler refuses a zero, and reports how many
      phrases the pairing takes to repeat, so the consequence of an edit
      is visible at `make music` time rather than eight minutes into a
      match
- [ ] 11.8 Percussion from TED's noise mode. It is voice 2's alternative,
      so it costs the drone while it sounds - needs a decision, not just
      code
- [ ] 11.9 Let the bed react to the match: a piece one square from home
      or a capture threatened could pick the mode or the tempo

## Epic 15 — Pacing, and the debug build

Two things the migration created rather than inherited. The frozen edition
never had to decide how long an animation should take — the interpreter
decided — and it could not have afforded a trace facility at all.

- [x] 15.1 **Every animation length is now a stated number.** The dice
      tumble is 16–21 scatter frames four frames apart (1.3–1.7 s, where
      it was 0.6); the curtain is 14 columns at 12 frames; the trophy is
      poured at 6 frames a row and engraved at 4 frames a letter. All in
      raster frames, none in loop iterations
- [x] 15.2 **Two build targets.** `make` is production; `make debug` is
      the same sources and the same optimiser flags plus `-DDEBUG`. The
      trace costs **3 135 bytes in the debug build and zero in
      production** — `dbg.c` is an empty translation unit without the
      define, and every macro expands to `(void)0`
- [x] 15.3 Trace lines carry a **depth column and a three-letter
      subsystem** (`gam brd rul dic bot mus frt txt`), so a log can be
      filtered by eye before reaching for the filter key
- [x] 15.4 A **shadow return stack**: `dbg_enter`/`dbg_leave` keep the
      path the program thinks it is on, and `f3` prints it joined —
      `gam>turn>sweep`. That is the question worth answering when a
      machine has stopped
- [x] 15.5 The sink is **injected at boot** rather than compiled in:
      `dbg_init (&dbg_screen_sink)` in `main`, or `&dbg_null_sink` for a
      headless run that wants the breadcrumb kept and nothing drawn
- [x] 15.6 Keys: `f1` overlay, `f2` filter one subsystem, `f3` breadcrumb,
      `f4` step. Swallowed inside `poll_key`, so no screen in the game
      knows they exist
- [x] 15.7 `DBG_BOUND` — an index reported when it leaves its range. On a
      machine with no memory protection an out-of-bounds write does not
      fault, it changes whatever the linker put next, which is why such a
      bug appears somewhere different every rebuild
- [x] 15.10 **The tile shading is a profile, not four constants.** The
      frozen edition's tile spends five of the machine's eight luminance
      steps on one 4x4 square (`+2` highlight, `-3` shadow); `SHADE_SOFT`
      halves that to three. `board_init` leaves `SHADE_LEGACY` in place so
      `demo.c` - the conformance oracle - needs no special case, and the
      game opts in at one line in `main`
- [x] 15.11 **The shimmer.** A nine-column band of light crossing the
      board left to right, lifting the highlight row of the tiles it
      passes and touching nothing else. Colour RAM only, so it can never
      disturb a glyph or a piece; 96 cells a frame off a 24-byte table
      built once. The quieter tiles are what gave it room to move
- [x] 15.12 **One luminance band, application-wide.** `LUM_PICK_MIN`/`MAX`
      in `plus4.h`; the colour picker offers 3-6 rather than 0-7 and
      URBOT obeys the same range. Below 3 a piece sinks into the tile it
      stands on whatever hue it is; at 7 it competes with the rosette
      stars and the shimmer's own highlight
- [x] 15.13 **Whose turn it is, said in light.** The side that is not to
      move has its four rows of squares and its waist plaque taken down
      three luminance steps, and the shimmer stops crossing them; the side
      that is stands in full colour with its stats legible. Done inside
      `paint_button`, so every repaint respects it - a piece landing on a
      dimmed square lands dimmed - and floored at 2 on the plaques,
      because the other side's piece count is still information you need
- [x] 15.14 **The folded-leaf shimmer.** The first sweep was a vertical
      band, which is what a spotlight on a flat sheet does and looked like
      it. A cell's place in the wavefront is now `x + 2*|y - vein|`, so
      rows further from the crease meet the light later and the front
      comes across as a chevron. The board's vein sits between the two
      halves of the bridge, where the game's own symmetry already is. The
      shadow row runs four steps behind its own highlight row - the far
      face of the fold catching the light after the near one
- [x] 15.15 **The travelling flash.** A piece no longer appears on the
      next square. The square it leaves goes black, flashes white and
      returns to its own colour; the square it arrives at flashes white
      and settles into the mover's colour. Three kinds, and the difference
      is the point: *pass* (vacated or jumped over - keeps its colour,
      because nothing changed hands), *claim* (white, then the mover's
      colour), and *battle* (red, white, red, black, then the winner's -
      a fight resolving rather than a handover). `square_tint` re-derives
      the occupant on every one of the dozen repaints rather than caching
      a "before" image, which is the same discipline as the rest of the
      renderer: ask the game state, never a copy of it
- [ ] 15.8 Move the trace panel off the casting floor. It currently shares
      rows 13–24 with the lots and the chronicle and they draw over each
      other
- [ ] 15.9 A trace sink that writes to a file over the SD2IEC, so a long
      run leaves a log instead of twelve lines
- [ ] 15.16 **The travelling flash is not verified end to end.** Each
      stage renders correctly — a harness paints a square black, white,
      battle red and claimed, and the dimmed band alongside it — and the
      sequencing is straight-line code, but no live screenshot has caught
      the sequence mid-move: the window is 8–24 frames a step and every
      sampling interval tried landed either side of it. A debug hook that
      parks on a nominated flash stage would close it
- [ ] 15.17 A `DBG_BOUND` on every `rowtab[]` subscript, not the four that
      have one. Cheap in the traced build, free in production, and the
      single most likely place to catch epic 16
- [ ] 15.18 **`make debug` does not link and the trace facility is
      therefore unavailable.** Not a fault in `dbg.c`: the traced build is
      ~3.1 KB larger and the transcribed beds are sized to fill RAM, so
      RODATA overflows by ~1.6 KB. Found 2026-08-08; row A3 of the test
      procedure has been red without being run. Three ways out, and the
      choice is a design decision rather than a fix:
      **(a)** a second `MIDBUDGET` for the debug build — one line, and the
      traced build then plays fewer beds, which nobody minds because it is
      an instrument;
      **(b)** shrink the 2 KB C stack (`__STACKSIZE__` in cc65's
      plus4.cfg) — 512 bytes are almost certainly free there, but on this
      machine a stack that overflows corrupts .bss silently, so it needs a
      high-water measurement first;
      **(c)** spend beds. (a) is the cheap one and does not touch what a
      player hears. **This blocks epic 16**, which is the wedge, and the
      wedge is what the trace facility was built for

## Epic 16 — The demo wedge (open)

- [ ] 16.1 **The URBOT demo wedges.** Left running headless, a match
      sometimes stops dead: the screen stops changing entirely and stays
      byte-identical over fifteen further emulated minutes. Matches also
      often run to a normal finish, so it is state- or timing-dependent,
      not systematic
- [ ] 16.2 What is known. Heartbeat counters written from inside each of
      `wait_frames`' two raster spins, and on either side of
      `music_service`, **all stop**, so the CPU is not in any of them.
      A phase marker in the turn loop caught it once inside `turn_sweep`
      and once inside `throw_lots` — two different places in two builds,
      which is the signature of memory corruption rather than of a loop
      that will not terminate. `DBG_BOUND` on the lot coordinates, the
      glide endpoints and the piece indices has not caught it
- [ ] 16.3 What is ruled out. It is **not** caused by servicing the music
      from inside `wait_frames`: that was suspected, backed out, and the
      wedge reproduced without it. It is not `blit_run` with a zero count
      (that case is handled), and not the arpeggio table overflowing
      (bank indices are masked)
- [x] 16.4 ~~**It got worse, and the smoke test now fails.**~~ **No
      longer true, and the reasoning was sound but the conclusion is
      superseded by 16.10.** Kept because the inference - a fault that
      relocates on every rebuild depends on what the linker put next - was
      exactly right, and pointed at the layout a fortnight before anyone
      worked out which layout. Before the
      tile and shimmer work the demo reached a normal victory at about
      1.5e9 cycles; it now wedges before 1.5e8, which is early enough that
      `make test` no longer produces a live screenshot. Removing the
      shimmer call does **not** move it back, so it is the layout change
      and not the feature — which is itself further evidence for the
      corruption hypothesis, since a bug that relocates on every rebuild
      is a bug that depends on what the linker put next
- [ ] 16.5 An IRQ heartbeat (`inc $0FE7` in `irq.s`) next to a main-line
      one showed **both still ticking** — but in a build that did not
      wedge at the points sampled, so it settles nothing. It is the probe
      to re-run once 16.6 makes a wedging build reproducible
- [ ] 16.6 **Make the demo deterministic first — this is the next step.**
      `rnd_stir()` folds the raster position and the timer into the seed
      on every `poll_key`, so every build plays a *different match* and no
      two builds can be compared. Stub it out behind a `-DFIXED_SEED` and
      the same match plays in every build: instrumentation can then be
      added freely without moving the trajectory, and the wedge can be
      bisected instead of hunted. Nothing else in this epic is worth
      attempting before it
- [ ] 16.7 Then, in order: a canary byte after each `.bss` array checked
      once a turn; `DBG_BOUND` on every `rowtab[]` subscript rather than
      the four that have one now; and a look at whether the raster
      handler's `jmp (old_irq)` chain can run with the KERNAL ROM banked
      out, since cc65's plus4 runtime keeps it banked out except around
      its own wrappers
- [x] 16.8 ~~`make test` is **red** until this is fixed~~ - it is green,
      and it stayed in the gate rather than being quietly dropped, which
      is the part of this item that mattered.

      It was red twice for two different reasons and only the second was
      the game. The first was the wedge. The second was the harness: once
      the attract timeout started working, the smoke shot at 600e6 cycles
      landed inside `turn_sweep`, and the "at least 15% black" predicate
      threw away a perfectly healthy frame six times running, because VICE
      under `-warp` reproduces the same frame on every retry. It now tests
      for the failure - the BASIC boot screen, 57% white against under 3%
      for any running program - rather than for a colour a live match is
      not obliged to be.

      Worth recording that it had also been passing for the wrong reason:
      with the frame counter frozen the attract timeout never fired, so
      the smoke test was screenshotting the MENU, which is black-backed
      and passes, and never reached a game at all. The gate asserted the
      opposite of its stated purpose

- [ ] 16.9 **`make test` now passes, and that is evidence rather than a
      fix.** After the TED keyboard scanner landed, four separate matches
      each ran to 6e8 cycles and produced a live screenshot of a game in
      progress — against 16.4's "wedges before 1.5e8".

      **Read the method before the result.** The first attempt at this ran
      the same binary four times and got four *identical* screenshots,
      because VICE is deterministic: same binary, same emulator, same
      match. That is one sample wearing four coats. Genuinely different
      matches were obtained by building at four different
      `ATTRACT_FRAMES`, which moves when the demo starts and so changes the
      whole trajectory; those four hash differently and all four survive.

      **A HYPOTHESIS, NOT A CLAIM.** `poll_key` used to call conio's
      `kbhit()` on every single poll - the most frequently executed call in
      the program - and conio reaches the KERNAL, which means banking. Our
      raster interrupt fires 200 times a second regardless. An interrupt
      landing inside a banking window is the kind of fault that stops the
      whole machine dead rather than looping, which is exactly 16.2's
      signature: every heartbeat stopped, in two different places in two
      builds. Removing `kbhit()` from the hot path removes that interaction
      entirely.

      **What would refute it:** the wedge is layout-sensitive (16.4), and
      this change moved the layout a great deal - a new module and 64 more
      bytes of interrupt tables. A build that merely landed somewhere
      lucky would look exactly like this. 6e8 cycles is also well short of
      the 1.5e9 at which victories used to be reached.

      **The test that would settle it:** put `kbhit()` back in `poll_key`
      alongside `kbd_get()`, changing nothing else, and see whether the
      wedge returns at the same four attract timings. If it does, the
      mechanism is banking and this epic is closed. If it does not, this
      was luck and 16.6 is still the next step

- [x] 16.10 **The mechanism was banking. 16.9 named the right hazard and
      the wrong half of it.** 16.7's last line wondered whether the raster
      handler's `jmp (old_irq)` chain could run with the KERNAL ROM banked
      out. The chain was fine. What was not fine is that the handler was
      entered through `$0314`, and cc65's plus4 stub switches the ROM back
      **in** before it gets there — so the handler read every one of its
      own variables out of BASIC ROM and wrote them to the RAM underneath.
      Once the program passed ~30 KB the linker put `irq.s`'s `.bss` above
      `$8000` and that became true.

      Measured, not inferred: a probe read `music_frames` = 33, `dbg_ent`
      = 22, `dbg_our` = 21, and `basic-318006-01.bin` holds 32, 21 and 20
      at those addresses. A fourth counter the handler never touched read
      its real RAM value of 0 rather than ROM+1.

      The handler now takes the processor's own vector at `$FFFE`, in RAM,
      and is never entered through the KERNAL. In the running game it
      counts **199 frames in 200** raster frames, at four entries a frame.
      Before the change it counted none at all, ever, in any build whose
      BSS landed above `$8000` — which is why it "stopped after 34 frames"
      and why it moved whenever unrelated code was added

- [ ] 16.11 **Whether this is also the wedge is not yet established.**
      What is established: with the counter fixed, the attract timeout
      fires, the demo starts, and a match ran to **9e8 cycles** still
      advancing — pieces moving, captures logged, the chronicle scrolling
      — against 16.4's "wedges before 1.5e8".

      That is one long run, and 16.9's own warning applies to it exactly
      as much: a build that merely landed somewhere lucky looks like this.
      It is also not obvious *how* the fault would wedge a CPU rather than
      merely make the music and the timers fictional; the garbage it put
      into `$FF12` reconfigures the character generator, which corrupts the
      picture without stopping the processor. So the honest position is
      that a defect certainly present has been removed and the wedge has
      not been reproduced since.

      **The test that would settle it** is still 16.6 — a fixed seed, then
      the same match played at several cycle budgets in a row

## Epic 12 — The speed pass (deliberately last)

Everything here is known headroom, quantified in
[`docs/lab-report.md`](docs/lab-report.md) §VI‑B. None of it is urgent:
the first pass is already 194–577×, and each item risks the conformance
test that makes the port trustworthy.

- [ ] 12.1 Move `paint_button` into `blit.s`. The board draw is ~54 cycles
      per cell against the blitter's 25-cycle floor, and most of the gap
      is cc65's six-argument call
- [ ] 12.2 Batch the gutter pass: 90 single-cell `blit_run(1)` calls each
      pay full pointer setup for one byte of work
- [ ] 12.3 Re-measure and re-run `make conform` after each. A speed change
      that alters a pixel is a bug, not an optimisation
- [ ] 12.4 Consider whether the frozen edition's cost-driven decisions
      should now be reverted — gutters-only painting, the split plaque
      painters — or kept because they express ownership rather than
      thrift. Probably kept; the point is to decide rather than inherit

## Epic 13 — Hardware verification

- [x] 13.1 **Deploy to the SD2IEC card and boot on the restored Plus/4 —
      done 2026-08-07.** `make card` writes the `.prg` and `.d64` to the
      card's `dev/` folder and its root, verifies every copy with `cmp`
      after `sync`, and refuses to report success otherwise; `make
      card-probe` puts kbdiag, kbtype and kbtest alongside them. The game
      loads with `dload"urfinkel" : run` and runs. No defect observed in
      that session. This is the project's first hardware validation of any
      kind and 13.2-13.4 below are still open
- [ ] 13.2 Re-verify the behavioural specification S1–S9 on hardware, as
      the frozen edition's epic 7.3 still asks
- [ ] 13.3 Re-run the on-target self-test on the machine (the frozen
      edition's 15.12, inherited)
- [ ] 13.4 Confirm the timings on real hardware. Every number in the lab
      report is from emulation, and the report says so

## Epic 14 — What the speed buys

Features the frozen edition's own documentation lists as out of scope
**specifically on performance grounds**. They are the point of the
migration, and they come after parity, not before it.

- [x] 14.1 Background music during play (epic 11.3)
- [ ] 14.2 Joystick piece selection with a cursor over the movable pieces.
      The frozen edition calls selection-by-number "a deliberate
      quick-delivery trade" and notes there is "no cursor layer to keep
      performant on a 1 MHz-class interpreter". There is now
- [x] 14.3 Smooth piece movement rather than one cell per delay loop
- [ ] 14.4 A board draw that is instant enough to redraw on demand,
      which removes the last reason the rules screen and the game screen
      have to share a renderer state

## Icebox

- [ ] Masters / Blitz rule variants (longer path, 3 dice, 0→4 move) — now
      cheap, since the path model is a table and the rules are testable
- [ ] Betting variant (coin pot on skipped/landed rosettes)
- [ ] High-score / win-tally file on the SD2IEC
- [ ] Bitmap mode with real sprites for the pieces
- [ ] A C16 build. cc65 has a `c16` target and the game fits; the 16 KB
      machine is the interesting constraint, not the Plus/4
