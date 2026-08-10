# The Keyboard Path in UR FINKEL: Trace, Hierarchy, and a Measured Fault

**Author:** Paul Richeson ([vonglurt](https://github.com/vonglurt)) — contact: paulr@sdf.org
**Date:** August 2026
**License:** MIT (see `LICENSE`)
**Project:** `urfinkel` — the compiled edition of the Royal Game of Ur
**Status of the finding:** measured under emulation and confirmed by hand — a
human pressing `3` at the menu on 2026-08-07 reaches the two-player setup and
plays a match. Not yet re-taken on hardware.

---

## Abstract

This report traces every path by which a keypress can reach UR FINKEL,
states the hierarchy of call sites that depend on it, and records a
measurement that identifies why keys do not arrive. The reported symptom
is that typed input — the name prompt, the colour picker's numbers, the
piece-selection digits — does nothing. The project's standing hypothesis,
carried in `backlog.md` §C, was that the game's own raster interrupt was
starving the KERNAL's keyboard scan. **That hypothesis is refuted.** A new
host-side probe shows the KERNAL's periodic interrupt does not run under
cc65's `plus4` runtime **whether our raster interrupt is installed or
not**, while our own handler counts frames normally. Since that same
KERNAL interrupt is what scans the matrix and fills the buffer count at
`$EF`, and `kbhit()` reads `$EF`, no key can reach the program by the
route the game currently uses. The fault is therefore below UR FINKEL
entirely — outcome **C5** in the existing procedure, the one branch no
previous test in this project could distinguish. The fix does not repair
the KERNAL but routes around it: TED scans this machine's keyboard itself,
and the `$FD30`/`$FF08` protocol was confirmed on the machine against six
keys, all six agreeing with the documented matrix. `poll_key` now reads
`kbd.c` instead of `conio`. Two instrument failures encountered on the way
are recorded in §VI‑B, because both produced confident wrong output.

**Index Terms** — Commodore Plus/4, TED, cc65, KERNAL, interrupt chaining,
keyboard matrix, input latency, testability, diagnosis.

---

## I. Why this needed a report rather than a fix

The keyboard has been an open question in this project for some time, and
the reason it stayed open is recorded honestly in `backlog.md` §C: the
path has two stages and only one of them could be tested.

```
   stage 1   the KERNAL's interrupt scans the matrix and bumps $EF
   stage 2   kbhit() reads $EF, cgetc() takes the character
```

Stage 2 was proven: VICE's `-keybuf` writes `$EF` directly and the menu
responded correctly at every delay tried. Stage 1 was declared untestable
from the host **for exactly the same reason** — writing `$EF` by hand is
precisely what jumps over it. So every headless keyboard test this project
could write would pass whether stage 1 worked or not, and the only
instrument for stage 1, `kbtest.c`, needs a human on the bench holding a
key down.

That is a genuine gap, and it is the reason three mutually exclusive
diagnoses (§C's C3, C4, C5) had been sitting undecided. They call for
completely different fixes, and guessing between them is how an afternoon
becomes a week.

## II. The hierarchy, traced

### A. The chokepoint

Every key in the production game passes through exactly one function,
`poll_key` in `game.c`:

```c
static unsigned char poll_key (void)
{
    unsigned char k;

    music_service ();
    rnd_stir ();
    k = (unsigned char)(kbhit () ? cgetc () : 0);
    if (k && DBG_KEY (k)) return 0;
    return k;
}
```

Nothing else in `game.c`, `board.c`, `front.c`, `dice.c` or `urbot.c`
reads the keyboard. That single chokepoint is the reason this report can
be short: there is one place to trace, not twenty.

### B. The tree above it

```
  poll_key ──────────────────────────────── non-blocking, returns 0
      │
      ├── menu()                            polls; falls through to the
      │                                     attract timeout if nothing
      │                                     ever arrives
      ├── demo_quit()                       polls; space leaves the demo
      │
      └── wait_key() ─── while (!(k = poll_key())) ;   ← BLOCKS
              │
              ├── input_line()          name entry, and the cursor
              │       └── read_num()    ← the colour picker's numbers
              │               └── colour_pick()   hue 1-16, shade 3-6
              │
              ├── colour_pick()         the keep-it? y/n loop
              ├── human_choose()        ← the piece-selection digits
              ├── manual_roll()         ← the manual tip count 0-4
              ├── rules_screen()        space to return
              └── victory()             press space; play again y/n
```

**The distinction that matters is blocking versus not.** `menu` and
`demo_quit` call `poll_key` directly and carry on when it returns 0.
Everything else goes through `wait_key`, which is `while (!(k =
poll_key())) ;` — an unbounded spin that has no exit other than a
keystroke.

### C. What that predicts if no key ever arrives

This is worth stating before the measurement, because it is a prediction
the reported symptoms can be checked against:

| Screen | Route | Behaviour with a dead keyboard |
|---|---|---|
| the cabinet menu | `poll_key` | **alive** — lamps chase, and after two minutes the attract timeout starts the demo by itself |
| the URBOT demo | `poll_key` | **alive** — plays a whole match; space never leaves it |
| name entry | `wait_key` | **hangs forever**, cursor blinking |
| colour picker | `wait_key` | never reached |
| piece selection | `wait_key` | **hangs forever** on a human turn |
| rules screen | `wait_key` | drawn, then never returns |
| victory | `wait_key` | drawn, then never returns |

So a dead keyboard produces a machine that looks *entirely healthy* — a
chasing marquee, a demo that starts on its own and plays a complete game
— and yet cannot be played. Note in particular that `backlog.md` B11,
"leave it alone for two minutes at the menu, the demo starts by itself",
is described there as "the cleanest proof the program is still running".
It is, and it passes, and it is also exactly the one check in the whole
bench procedure that a totally dead keyboard cannot fail.

### D. The route below `kbhit`

```
  poll_key
    └── kbhit()            cc65 conio: reads KEY_COUNT
          └── $EF          plus4.inc: KEY_COUNT := $EF
                └── written by the KERNAL's periodic interrupt
                      └── which scans the matrix through TED $FF08
```

Three details about the bottom of that stack are specific to this machine
and are easy to get wrong from C64 habit:

1. **The keyboard is TED's, not a CIA's.** The Plus/4 has no CIA keyboard
   matrix at `$DC00`/`$DC01`. TED scans it, and the latch is at **`$FF08`**
   (`plus4.inc:63`, `TED_KBD := $FF08`).
2. **The buffer count is `$EF`.** Not `$C6`/198, and the buffer is not at
   631–640. Those are the **C64's** `NDX` and `KEYD` (`c64.inc:17` has
   `KEY_COUNT := $C6`). On the Plus/4 the count is `$EF` and the buffer is
   at `$0527`. A probe reading 198 on this machine reads an unrelated byte
   and will report confident nonsense.
3. **cc65 reaches the keyboard through `conio`, never the jump table.**
   `game.c` says why in a comment, and the lab report records it as one of
   the four porting defects: cc65's `plus4` runtime keeps the KERNAL ROM
   banked out except around its own wrappers, so a naked `jsr $FFE4`
   executes whatever RAM lies underneath.

## III. Method: testing stage 1 without pressing a key

The trick that opens up stage 1 is to stop trying to observe the keyboard
and observe **the interrupt that scans it** instead.

The KERNAL's periodic interrupt does two jobs on every tick: it scans the
keyboard, and it advances the jiffy clock at `TIME` (`plus4.inc`: `$A3`,
three bytes). The clock needs no keystroke, no buffer, and no `-keybuf` to
observe. So:

> the jiffy clock advances → the KERNAL handler is running → the keyboard
> is being scanned
>
> the jiffy clock is frozen → it is not, and no key can arrive however
> hard it is pressed

That converts an untestable question into one a screenshot answers.

`src/kbdiag.c` measures the clock across two 100-frame windows — one with
our raster interrupt **removed**, one with it **installed** — and prints a
verdict. The A/B is the whole point: it separates "our interrupt broke it"
from "it was never running".

Two deliberate properties of the instrument:

- **All three bytes of `TIME` are watched**, not just one. The verdict
  rests on "this counter is not moving", and a counter also appears not to
  move when the wrong end of it is read — the byte that ticks 50 times a
  second and the byte that ticks once every five minutes are
  indistinguishable over a two-second window. Reading all three removes the
  one way this program could lie.
- **The window is timed on the raster, not on the clock.** A frozen clock
  has to end the window, not hang inside it.

```sh
make kbdiag-shot        # headless; build/kbdiag.png carries the verdict
make kbtype-run         # the human half: a prompt, both routes side by side
```

## IV. Results

From `build/kbdiag.png`, VICE `xplus4`, PAL, cc65 2.19, `-Osir -Cl`:

| Window | `TIME` `$A3` | `$A4` | `$A5` |
|---|---:|---:|---:|
| our raster IRQ **off** | 0 | 0 | 0 |
| our raster IRQ **on** | 0 | 0 | 0 |

| Also observed | Value |
|---|---:|
| frames counted by **our own** interrupt | 100 |
| `NDX` (`$EF`) | 0 |
| keys read through `kbhit`/`cgetc` | 0 |
| TED `IMR` (`$FF0A`), our raster bit cleared | 160 (`$A0`) |
| TED `IRR` (`$FF09`) | 127 (`$7F`) |

**The KERNAL's clock does not advance in either window, in any of its
three bytes, while our own interrupt counts a clean 100 frames over a
100-frame window.**

### A. What each number rules out

- Interrupts are **not** globally disabled: our handler ran 100 times.
- The measurement is **not** a byte-order artefact: all three bytes are
  frozen.
- Our raster interrupt is **not** the cause: the clock is equally frozen
  with it removed. This is the control, and it is the one the previous
  hypothesis needed to fail.
- `NDX` at 0 after two seconds is consistent, but on its own proves
  nothing here — no key was pressed in a headless run. It is the clock
  that carries the finding.

### B. The verdict, in the procedure's own terms

`backlog.md` §C asks the bench to report which of three outcomes it sees.
This is **C5** — "the fault is below us entirely: cc65's runtime, the
KERNAL banking, or the machine" — and it is now established from the host,
which §C states outright is impossible. That statement was true of every
test that works by pressing keys; it is not true of one that watches the
clock.

C4 — "our raster interrupt is starving the KERNAL's keyboard scan, the fix
is in `irq.s`" — is **refuted**. Any time spent on `handler`'s chaining to
`old_irq` would have been spent on code that is not what is wrong.

### C. The mask register, and how far to trust it

`IMR` reading `$A0` with our raster enable cleared says the timer
interrupt enables are largely clear, which is consistent with cc65's
startup masking off the KERNAL's periodic interrupt for the duration of
the program. **This bit-level reading is provisional**: the exact TED
`IMR` bit assignment is quoted here from memory rather than from a datasheet
this report has checked, and nothing in §IV‑A depends on it. The frozen
clock is the finding; the mask register is a lead about the mechanism.

## V. How the keyboard fits the whole program

`poll_key` is not only an input function, and this matters both for
understanding the failure and for fixing it. It has **three**
responsibilities, and only one of them is the keyboard:

1. **`music_service ()`** — the cooperative half of the sequencer. The
   ambient bed only advances when something polls or waits *live*; this is
   one of the two places that happens (`wait_frames_live` is the other).
2. **`rnd_stir ()`** — folds TED's raster position into the random seed.
   This is the program's entropy source, and it is also, per backlog 16.6,
   the reason every build plays a different match and the demo wedge cannot
   be bisected.
3. **`DBG_KEY (k)`** — swallows `f1`–`f4` so that no screen in the game has
   to know the trace keys exist, and so the production build carries no
   branch for them.

Two consequences follow directly:

- **A dead keyboard does not stop the music or the randomness.** Both run
  before `kbhit` is consulted and neither depends on its result. So the
  wedge in epic 16 and the non-determinism in 16.6 are untouched by this
  finding — they remain exactly as open as they were.
- **Whatever replaces `kbhit` must not disturb those two.** The fix belongs
  *inside* `poll_key`, replacing one line, and not at the twenty call sites
  above it. That the project already funnelled every key through one
  function is what makes this a small change rather than a large one.

Note also what does **not** poll: `wait_frames` and `wait_frames_live`
service the music and the shimmer but never read a key. Keys pressed
during an animation are not lost — they accumulate in the KERNAL's buffer
and are picked up at the next poll — *provided the buffer is being filled
at all*, which §IV says it is not.

## VI. The route that does not need the KERNAL

Because TED scans this keyboard itself, the matrix can be read without the
KERNAL being involved. That makes the fix structural rather than a repair:
stop asking a handler that demonstrably does not run, and read `$FF08`
directly from code this project already owns.

It fits this program unusually well:

- the game already owns a raster interrupt at 200 Hz, which is a far
  better scan rate than the KERNAL's 50 Hz;
- every other TED register in the project is already addressed directly
  and marked `volatile` in `plus4.h`;
- it removes the dependency on `conio` — and with it the last thing in the
  hot path that assumes anything about ROM banking;
- it is the precondition for backlog 14.2 (joystick selection) anyway,
  since that too is read straight from the hardware.

The cost is a keycode table and a decision about repeat handling, both of
which the KERNAL was previously providing for free.

### A. The protocol, confirmed on the machine

Two candidates were run side by side rather than guessed between:

```
   via $FD30   write the row mask to the scan latch, strobe $FF08, read
   via $FF08   write the row mask straight to $FF08, read it back
```

`src/kbhunt.c` asked for the six keys the menu needs and recorded the
whole eight-row matrix for each. Measured on the machine:

| key | row | value | bit low |
|---|---:|---:|---:|
| `1` | 7 | 254 (`$FE`) | 0 |
| `2` | 7 | 247 (`$F7`) | 3 |
| `3` | 1 | 254 | 0 |
| `4` | 1 | 247 | 3 |
| `5` | 2 | 254 | 0 |
| `m` | 4 | 239 (`$EF`) | 4 |

Every one matches the documented Plus/4 matrix — row 7 is
`1 HOME CTRL 2 SPACE C= Q STOP`, row 1 is `3 W A 4 Z S E SHIFT`, row 2 is
`5 R D 6 C F T X`, row 4 is `9 I J 0 M K O N`. Six of six agreeing is what
licenses `src/kbd.c` using the **whole** table rather than only the keys
that were pressed.

**The `$FD30` protocol is the one this machine honours.** The fix is
implemented: `poll_key` calls `kbd_get()`, and `music_service` and
`rnd_stir` stay exactly where they were.

### B. Two instrument failures worth recording

Both produced confident, wrong output, which is the only kind of
measurement error that matters:

1. **Bit 7 is not a keyboard column here.** The first probe read 127
   (`$7F`) with nothing held, so its "is anything down?" test - `!= 255` -
   was true forever. Every capture fired on the settle timer instead of on
   a keypress. `kbd_init` now samples the idle matrix at boot and ignores
   whatever is low then, so the scanner cannot be fooled by it however it
   arose.
2. **The first hunt recorded a summary instead of the measurement.** It
   kept only "the first row that was not 255, and what it read", and
   produced a table in which `1`, `2`, `3` and `5` all read 254 - four keys
   with one signature, which decodes nothing. A key is a (row, column)
   intersection and no single byte can name it. The full eight-byte
   signature is what the table above is.

The second is the more instructive: a lossy summary of a measurement looks
exactly like data, and is worse than returning nothing at all.

### C. Column 7, and the third instrument failure

The `127` of §VI‑B(1) raised a real worry: bit 7 carries `SHIFT`, `X`, `V`,
`N`, `,`, `/`, `@` and `STOP`, and `kbd_init` permanently ignores whatever
is low at boot - so a genuinely stuck line would silently kill all of them,
including the `n` of the colour picker's `y`/`n` prompt.

**It is not stuck.** `make kbdiag-shot` now also prints the idle matrix,
and in a headless run - where by construction nothing is held - all eight
rows read 255:

```
idle matrix - all 255 = nothing stuck
255 255 255 255 255 255 255 255
clean - every key is reachable
```

So the earlier 127 was a third instrument failure, not a hardware fault:
that probe strobed `$FF08` **without first selecting a row through
`$FD30`**, and was reading a latch nobody was driving. Select a row and the
line reads high like every other. `kbd_init`'s calibration consequently
masks nothing, and the whole matrix is available.

This is also why the idle check is worth keeping rather than deleting now
that it has answered once: it is the assertion that the calibration is a
no-op, and if that ever stops being true on real hardware the keys it
silently removes would otherwise be very hard to account for.

### D. The scan rate is the debounce

Everything above is about *reading* the matrix, and all of it held. The
menu stayed unresponsive anyway, and the reason was in the half nobody had
instrumented: `kbd_get` both scanned and decoded, so how often it scanned
was decided by whichever loop happened to be calling it.

| caller | how often it scanned | two consecutive scans |
|---|---|---|
| `wait_key` | a bare `while (!(k = poll_key ()));`, no frame wait | microseconds |
| `menu` | once per loop, and its loop is `wait_frames_live (4)` plus the lamp chase | 150-250 ms |

The debounce asks a key to read down on two consecutive **scans**. At the
first rate that is no debounce at all, and it is where the phantom
keypresses came from. At the second it demands the key be held for a third
of a second, so an ordinary tap was never seen twice and never arrived.
One mistake, two opposite symptoms: **a debounce counted in calls rather
than in time.**

The scan is now `kbd_scan`, called once a frame from `wait_frames_live`
and from nowhere else - the same shallow wait the music sequencer is
serviced from, and the one every wait in the game passes through. That
makes the rate a flat 50 Hz everywhere and the debounce 40 ms. `kbd_get`
only takes from a small ring the scan fills, so a key pressed during a
curtain or a throw is still there when the game next asks; `wait_key` now
waits a frame per turn round its loop, because a spin that never reaches
`wait_frames_live` would never scan.

`test/test_kbd.c` covers this half on the host, under `make check`: the
eleven signatures, the debounce in both directions, edge-not-level, the
buffering, and the idle calibration. It fakes the matrix read and nothing
else, which is the only thing §III permits a host test to fake here - the
stage it fakes is the stage kbhunt measured on the machine, not the stage
under suspicion.

### E. What is still not covered

- **No SHIFT and no auto-repeat.** Names arrive lower case, which the
  renderer wanted anyway; held keys do not repeat.
- **`dbg.c`'s `f4` step mode still calls `cgetc()`** and would block
  forever. Debug build only.
- **Everything here is emulation.** `make kbdiag` runs on the bench too and
  should be re-run there.

## VII. Threats to validity

- **All of §IV is from emulation.** VICE is faithful enough that the
  cc65 startup path should behave identically on hardware, but this project
  has an open item to re-verify everything on the restored machine and this
  finding inherits it. `make kbdiag` runs on the bench too, and should be
  run there before the fix is designed around it.
- **The mechanism is inferred; only the symptom is measured.** What is
  established is that the KERNAL's interrupt does not run. *Why* — masked
  timer, banked-out ROM, or a startup path that never installs it — is a
  lead (§IV‑C), not a result.
- **`-keybuf` remains a false friend.** It will continue to make headless
  keyboard tests pass by writing `$EF` directly, and any future test that
  uses it is testing stage 2 only. This is why `kbdiag` does not use it.
- **No keystroke was pressed in the measurement.** That is the instrument's
  strength, not a gap — but it does mean `kbdiag` cannot confirm that keys
  *work* after a fix, only that the scan is running. `kbtype.c` and the
  bench procedure remain the final word.

## VIII. What to do next, in order

1. ~~Decide the strobe protocol~~ — **done**, §VI‑A.
2. ~~Replace the read in `poll_key`~~ — **done**; `kbd.c`, and
   `music_service`/`rnd_stir`/`DBG_KEY` are untouched.
3. ~~Settle column 7~~ — **done**, §VI‑C: nothing is stuck, the whole
   matrix is reachable, and the calibration masks nothing.
4. **Re-run `make kbdiag` after the fix** — it should be *unchanged*,
   because the fix routes around the KERNAL rather than reviving it. A
   changed result would mean something unintended moved as well.
5. **Amend `backlog.md` §C.** C4 is refuted and C5 confirmed; the section
   still asks the bench a question that has been answered.
6. ~~Fix the scan rate~~ - **done**, §VI-D: the debounce was counted in
   calls, not in time, which produced phantom keys in one loop and dead
   keys in another. `kbd_scan` now runs once a frame from
   `wait_frames_live`, and `test/test_kbd.c` holds it there.
7. **SHIFT and auto-repeat**, if name entry wants upper case.
8. **Re-verify on hardware** (epic 13). Every number here is from
   emulation and the report says so wherever it says a number.

## References

[1] `backlog.md` §C — the two-stage model and the C3/C4/C5 outcomes.
[2] `src/kbtest.c` — the bench instrument; the human half of §III.
[3] `src/kbdiag.c` — the host instrument introduced by this report.
[4] `src/kbtype.c` — the interactive probe; both routes side by side.
[5] cc65 2.19, `asminc/plus4.inc` — `KEY_COUNT := $EF`, `TED_KBD := $FF08`,
    `TIME := $A3`; and `asminc/c64.inc` — `KEY_COUNT := $C6`, for the
    contrast in §II‑D.
[6] `docs/lab-report.md` §IX‑D — the four porting defects, of which number
    4 is the KERNAL banking behaviour this report's §IV‑C points back at.

---

*Released under the MIT License. Copyright (c) 2026 Paul Richeson.*
