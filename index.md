# UR FINKEL — File Index

The intention of each file, and how the files derive from one another.

## Hierarchy

```
src/urfinkel/                 ← project root (inside the plus4 restoration repo)
├── index.md                  ← this map
├── README.md                 ← entry point: what, why, the numbers, build
├── LICENSE                   ← MIT, Paul Richeson
├── Makefile                  ← cl65 target build, host test build, VICE harnesses
├── start.sh                  ← launcher: brew-safe PATH, then make run
├── backlog.md                ← the ordered migration plan, epic by epic
├── docs/
│   ├── lab-report.md         ← IEEE-style migration report: baseline, chain, results
│   ├── architecture.md       ← target design: modules, memory, blitter, status
│   ├── music.md              ← two voices, one volume, and the raster interrupt
│   ├── keyboard-lab-report.md ← IEEE report: reading a matrix with no OS under it
│   ├── keyboard-report.md   ← the fault trace behind it, in full
│   └── project-review.md     ← what the migration delivered, cost, and got wrong
├── src/
│   ├── plus4.h               ← the machine: matrices, TED registers, character sets
│   ├── blit.s                ← ca65: one run of cells into both matrices
│   ├── irq.s                 ← ca65: the raster interrupt, four times a frame
│   ├── board.h / board.c     ← the renderer, ported cell for cell, plus `glide`
│   ├── rules.h / rules.c     ← the Finkel ruleset, free of the machine
│   ├── text.h / text.c       ← the writer and the chronicle
│   ├── dice.h / dice.c       ← randomness, the four lots, the tumble
│   ├── urbot.h / urbot.c     ← four doctrines over one opportunity scan
│   ├── music.h / music.c     ← the 50 Hz sequencer behind the interrupt
│   ├── song.h, song_ids.h    ← GENERATED from tools/songs.mml
│   ├── front.h / front.c     ← the cabinet: proscenium, lamps, curtains, trophy
│   ├── dbg.h / dbg.c         ← the trace facility; empty without -DDEBUG
│   ├── kbtest.c              ← the keyboard probe: a bench instrument
│   ├── game.c                ← the controller, the front end, the end of a match
│   ├── demo.c                ← draws the opening board and parks
│   └── bench.c               ← times the primitives against the jiffy clock
├── test/
│   └── test_rules.c          ← the frozen rule table, run by the host compiler
├── tools/
│   ├── mml.py                ← the music notation compiler
│   ├── songs.mml             ← the songs AND the ambient bed, as text
│   └── viceshot.py           ← headless screenshots that verify rather than hope
├── bench/
│   ├── basic-bench.bas       ← timing block appended to a copy of urroyal.bas
│   └── basic-board.bas       ← board-draw block appended to a copy of urroyal.bas
└── build/                    ← generated, git-ignored
    ├── urfinkel.prg / .d64   ← the game, and its boot disk
    ├── demo.prg / bench.prg  ← the conformance and benchmark builds
    ├── demo.png              ← the compiled board, screenshotted
    ├── basicboard.png        ← the BASIC board, screenshotted — must be identical
    ├── bench.png             ← the compiled timings, as the machine reports them
    ├── basicbench.png        ← the BASIC timings
    ├── basicbench.bas/.prg   ← the patched throwaway copy of the frozen source
    ├── basicboard.bas/.prg   ← ditto, for the conformance draw
    └── test_rules            ← the host test binary
```

## Intent of each file

| File | Intention |
|---|---|
| `docs/lab-report.md` | The *why* and the *how much*: the measured baseline (24 s board draw, 2.8 s move scan), the three interpreter properties that cause it, the evidence behind choosing cc65 over `llvm-mos`, a BASIC compiler and pure assembly, the first-pass results, and the argument that the removed constraint matters more than the ratio. Maintained, not archived — it carries a maintenance log and a table of which measurements have to be re-taken when what is touched. |
| `docs/keyboard-lab-report.md` | The keyboard, as a university-format report: the compilation environment tool by tool, the two ways to read a key on this machine, the BASIC prototype's thirteen `GET` sites and why they stopped working, the 194-577x recoding that surrounds it, the two faults (a KERNAL interrupt that never runs, then a debounce counted in calls rather than in time), what a host test may legitimately fake, and the first hardware run off the SD2IEC. |
| `docs/keyboard-report.md` | The fault trace the report above is built on: the two-stage model, the C3/C4/C5 diagnosis matrix, the jiffy-clock instrument that settled it without pressing a key, three instrument failures worth recording, and the measured matrix signatures. |
| `docs/music.md` | The *why it sounds like that*: TED's two voices and one global volume, arpeggiated chords and a drone, the hurdy-gurdy buzz that the shared volume register makes possible, the circle of fifths, the Fibonacci sparkle spacing and the 8-against-13 polyrhythm, and the text notation songs are written in. |
| `docs/architecture.md` | The *how*: what is inherited unchanged (the path model, the geometry, the rules), the module map and the host/target split that makes the ruleset testable, the memory map and the `$0400` invariant the blitter rests on, the rendering primitives, the planned animation and controller shape, and a component-by-component status table. |
| `backlog.md` | The *in what order*: conformance before features, features before speed. Fourteen epics from forking the project through hardware verification, ending with the features the frozen edition ruled out on performance grounds. |
| `src/plus4.h` | The machine's constants in one place: the two matrices and the fact they are `$0400` apart, the screen codes the board is built from, the TED registers this project touches, and the character-set codes — with the reason the upper-case set is not optional. |
| `src/blit.s` | The one primitive everything stands on: a run of identical cells written into the screen and colour matrices in a single pass, 25 cycles per cell, parameters passed as globals because cc65's stack convention costs more to unpack than the routine costs to run. |
| `src/board.c` | The renderer, ported constant for constant from `urroyal.bas` — the acceptance test is a screenshot diff, so cleverness here costs conformance. Also `glide`, the motion tweening the BASIC edition could not afford. |
| `src/irq.s` | The small program that runs between the raster lines: four times a frame it advances the arpeggio, writes voice 1, and modulates the one global volume. It makes no decisions and calls no C — cc65's zero page is not reentrant — and it never reads `$FF12`, whose upper bits configure the character generator. |
| `src/music.c` | The 50 Hz sequencer behind it: the beat grid, the envelope struck on the beat, the phrase arch, which bank each instrument is playing, the Fibonacci sparkles and their echoes, the written songs, and the effects that seize the volume register. |
| `docs/project-review.md` | The review: the 194-577x measurement and what it was spent on, where the design earned its keep and where it did not, the three cc65 traps recorded at the point they bit, the one open defect, and the order the remaining work should happen in. |
| `src/front.c` | The cabinet the game is played inside: the frieze bands, the chasing lamps, the proscenium, curtains that travel fourteen columns in the 168 frames the fanfare lasts, and the gold trophy poured a row at a time. Every delay in it is a stated number of raster frames, because compiled the machine no longer sets the pace by being slow. |
| `src/kbtest.c` | The keyboard probe. A key reaches the program in two stages - the KERNAL's interrupt scans the matrix and bumps `$EF`, then `kbhit()` reads it - and only the second can be tested from the host, because VICE's `-keybuf` writes `$EF` directly and so jumps over the first. This shows both at once and switches the raster interrupt on and off by itself every four seconds, because the keyboard is what is on trial and cannot be trusted to drive the test. `make kbtest && make card-probe`; procedure in `backlog.md` §C. |
| `src/dbg.c` | The trace facility, entirely inside `#ifdef DEBUG`: subsystem tags, a depth column, a shadow return stack printed as a breadcrumb, bound assertions, and an injected sink. 3 135 bytes traced, zero in production. |
| `src/game.c` | The controller and the front end. Every wait in it services the music and stirs the random state, so nothing ever blocks. |
| `tools/mml.py` | The music notation compiler: the pitch table, the written songs, and the ambient bed's banks and chord progression. Bank steps are notated as chord *degrees*, so a bank cannot name a note outside the chord and no pairing of banks can be dissonant. It also owns the fact that TED cannot sound below about 108 Hz, and reports every note it lifted an octave to clear it. |
| `tools/viceshot.py` | The screenshot harness. VICE's autostart loses the RUN keystroke about one run in eight, so this verifies the program actually ran — by the black background every screen in this project settles on — and retries. |
| `src/rules.c` | The Finkel ruleset with no machine in it, so it compiles for macOS as well as the Plus/4. That property is what `make check` is made of. |
| `src/demo.c` | Draws the opening board once and parks, so a screenshot of it can be compared against a screenshot of the frozen edition. |
| `src/bench.c` | The headline measurement: the same primitives the BASIC benchmark times, against the same clock. |
| `test/test_rules.c` | The frozen edition's thirteen-row rule table, carried across verbatim, with the seven executor assertions present but unasserted so landing the executor drops the deferred count to zero rather than growing the table. |
| `bench/*.bas` | The blocks appended to a *throwaway copy* of `urroyal.bas` whose line 300 becomes a jump into them — the frozen source is never modified. |
| `Makefile` | The *run it*, the *check it* and the *prove it*: `cl65 -t plus4 -Osir -Cl` for the target, the system compiler for the rule tests, and three headless VICE harnesses — timings, board conformance, and the emulator itself. |
| `README.md` | The front door — the numbers, the conformance story, and the build. |
| `index.md` | This file. |
| `LICENSE` | MIT terms covering code and documentation. |

## Derivation graph

```
  urroyal-basic/urroyal.bas  (frozen: the spec, the oracle, the baseline)
            │
            ├───────────────► bench/*.bas ──► build/basicbench.png   (the baseline)
            │                              └► build/basicboard.png   (the oracle image)
            │
            └── line 9600 rule table ──────► test/test_rules.c
                                                      │
  docs/lab-report.md   (records the baseline, the chain, the results)
            │
            ▼
  docs/architecture.md (turns the measurements into a design)
            │
            ▼
       backlog.md      (turns the design into ordered epics)
            │
            ▼
     src/*.c, src/*.s  (implements the epics)
            │
     ┌──────┴───────────────────┬──────────────────────┐
     ▼                          ▼                      ▼
  make check              make conform             make bench
  host cc, ms          demo.png == basicboard.png   both halves timed
     │                          │                      │
     └──────────────┬───────────┘                      │
                    ▼                                  ▼
              the change is allowed          the lab report's §VI table
```

- `docs/lab-report.md` **derives from** measurements of the frozen
  edition; if the frozen edition is bug-fixed under its maintenance
  policy, §III has to be re-taken.
- `docs/architecture.md` **derives from** the lab report — the design is a
  response to the profile, not to taste.
- `backlog.md` **derives from** the architecture; new design work lands
  here first as ordered steps.
- `src/rules.c` **must satisfy** `test/test_rules.c`, which is the frozen
  edition's own rule table. Neither file may be changed to make the other
  pass without the change also being made in the frozen edition and its
  golden race re-run.
- `src/board.c` **must satisfy** `make conform` — it is allowed to be
  faster than the BASIC renderer, not different.
- `bench/*.bas` and `test/test_rules.c` both **read from**
  `../urroyal-basic/`, which is why that directory is frozen rather than
  deleted.
- The parent repository's restoration docs (`../../docs/`) provide the
  toolchain (`06-macos-toolchain.md`) and transfer (`05-sd2iec-primer.md`)
  context this project's build and deploy paths rely on.
