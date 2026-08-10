# Reading a Keyboard Without an Operating System: Migrating an Input Path from Interpreted BASIC to Compiled 6502 on a Commodore Plus/4

**Author:** Paul Richeson ([vonglurt](https://github.com/vonglurt)) — contact: paulr@sdf.org
**Date:** August 2026
**License:** MIT (see `LICENSE`)
**Project:** `urfinkel` — the compiled edition of the Royal Game of Ur (Finkel ruleset)
**Companion documents:** [`lab-report.md`](lab-report.md) (the migration as a whole),
[`keyboard-report.md`](keyboard-report.md) (the fault trace, in full)
**Status:** all measurements from emulation unless marked *on hardware*;
the hardware run is 2026‑08‑07 on a restored Commodore Plus/4

---

## Abstract

A working program was prototyped in Commodore BASIC 3.5, re-implemented in
C for the same machine, and deployed to original hardware over an SD2IEC
mass-storage adapter. The re-implementation is between **194× and 577×**
faster per primitive, and **311×** on a composite turn. This report
concerns the one subsystem that did not survive the migration at all: the
keyboard. In the interpreted prototype, reading a key is a single token,
`GET`, and it works because the interpreter runs underneath a live
operating system whose periodic interrupt scans the key matrix and fills a
buffer. Compiled under `cc65`, that operating system is present in ROM but
not running, and the same buffer is never written; thirteen `GET` sites
became one C function that could never return a key. The subsystem was
rebuilt to read the hardware matrix directly through TED's `$FD30`/`$FF08`
scan latch, owing the KERNAL nothing. Two distinct defects were found and
measured: the operating system's interrupt does not run under the C
runtime at all, and — after that was routed around — a debounce that
counted *calls* rather than *time*, which in one loop scanned thousands of
times a second and in another once every third of a second, producing
phantom keypresses and dead keys respectively from the same four lines of
code. The final implementation is 169 lines of C, compiles to 4 723 bytes
of object code, is covered by 26 host-side assertions, and was confirmed
by a human pressing keys on a real Plus/4 loaded from an SD2IEC. The wider
argument is that a portability boundary is not only an API surface: `GET`
and `kbd_get()` have the same signature and the same contract, and the
difference between them is which machine is running when they are called.

**Index Terms** — retrocomputing, Commodore Plus/4, TED 7360, BASIC 3.5,
cc65, 6502 cross-compilation, keyboard matrix scanning, debouncing,
interrupt vectors, memory banking, embedded testing, SD2IEC.

---

## I. Introduction

The `urfinkel` project migrates a complete implementation of the Royal
Game of Ur from Commodore BASIC 3.5 to compiled 6502. The predecessor is
758 lines of line-numbered BASIC and is frozen as both the specification
and the conformance oracle. The migration's headline
result is reported elsewhere; the summary is in §IV‑A here because it sets
the context in which the keyboard failed.

Input was not identified as a risk in the migration plan. It is one line
of BASIC:

```basic
2670 getkey a$
```

and the plan assumed C's equivalent — `cc65`'s `conio` layer, offering
`kbhit()` and `cgetc()` — was the same thing spelled differently. It has
the same signature and the same contract. It is not the same thing, and
the difference is not visible in any source file.

This report documents what the difference was, how it was isolated, and
what it cost. It is written up separately from the migration report
because the finding generalises past this machine: **an API that reads
state maintained by somebody else has a dependency that its signature does
not mention**, and migrating the caller can silently remove the somebody.

## II. Materials: the compilation and test environment

Every tool is named with the version actually used, because two of the
findings below are properties of a specific toolchain rather than of the
machine, and a reader who cannot reproduce the toolchain cannot reproduce
the findings.

### A. Target toolchain

| Tool | Version | Role |
|---|---|---|
| `cc65` | 2.19 (binaries self-report `V2.18`) | C compiler for 6502 |
| `cl65` | — | compile-and-link driver; the only command the Makefile invokes |
| `ca65` | — | assembler, for `irq.s` and `blit.s` |
| `ld65` | — | linker; `plus4.cfg` places the program at `$1001` |

Flags: `cl65 -t plus4 -Osir -Cl`. `-Osir` is cc65's full optimiser
(optimise, inline known functions, use registers, inline more
aggressively). `-Cl` makes function locals static rather than
software-stack allocated, which matters on a 6502 because indexing that
stack is expensive.

**The 6502 code is compiled by `cc65`, not by `clang`.** No LLVM-based
chain was available: `llvm-mos` generates better 6502 code but its SDK has
no `plus4` or `c16` platform, checked against its `mos-platform` directory
listing. `cc65` is the only ready-made C chain for the 264 series.

### B. Host toolchain

| Tool | Version | Role |
|---|---|---|
| Apple `clang` | 21.0.0 (clang-2100.1.1.101) | host-side unit tests (`make check`) |
| Python | 3.9.6 | MML song compiler, screenshot harness |
| macOS | 26.5.2, arm64 | development host |

`clang` compiles the rule tests and the keyboard decoder test **for the
host**, in milliseconds, against a minute of emulation. It never produces
a byte that runs on the Plus/4. This division is deliberate and is
revisited in §VI‑B.

### C. Emulation and deployment

| Tool | Version | Role |
|---|---|---|
| VICE `xplus4` | 3.10 | PAL Plus/4 emulation, headless with `-warp -limitcycles -exitscreenshot` |
| `c1541` | (VICE 3.10) | packs the PRG into a D64 image |
| `petcat` | (VICE 3.10) | tokenises the BASIC predecessor for comparison |
| SD2IEC | — | SD mass storage on the serial IEC bus, device 8 |
| Commodore Plus/4 | PAL, restored | the target |

The SD2IEC is powered from external 5 V USB. It was sold as
cassette-port-powered *for the C64*; that power path does not exist on a
264-series machine, whose cassette connector is a 7-pin mini-DIN rather
than the C64's card edge.

## III. Architecture: two ways to read a key

### A. The prototype's path, and why it works

BASIC 3.5's `GET` and `GETKEY` take a character from a queue the operating
system fills. On this machine the KERNAL's periodic interrupt does two
jobs on every tick: it scans the key matrix, and it advances the jiffy
clock at `TIME` (`$A3`–`$A5`). A key pressed at any moment is therefore
already in the buffer by the time BASIC asks, and the count of waiting
characters lives at `$EF`.

The predecessor uses this thirteen times. It never scans anything, never
debounces anything, and never thinks about it, because a running operating
system is doing all of it.

### B. The compiled path, as planned

`cc65`'s `conio` offers the same shape: `kbhit()` tests `$EF`, `cgetc()`
takes the character. The port used it, and it was correct code.

### C. What is actually underneath

`cc65`'s plus4 runtime does not run the program under the KERNAL. Its
startup code, disassembled from the linked binary:

```asm
        sei
        sta $FF3F        ; RAM switched in over $8000-$FFFF
        ...
        ldx #$6B
        ldy #$10
        sei
        sta $FF3F
        stx $FFFE        ; the processor's own IRQ vector, in RAM
        sty $FFFF        ;   -> the runtime's own stub at $106B
        cli
        jsr main
```

The program runs with RAM banked over the whole upper half of memory, and
with the hardware interrupt vector pointing at a stub of the runtime's
own. That stub switches the ROM back in and chains onward only so that
KERNAL interrupt code *can* run — but nothing enables the KERNAL's timer
interrupts, and measurement confirms it: TED's interrupt mask register
`$FF0A` reads `$A2`, in which only the raster bit is set. The KERNAL's
periodic interrupt is masked off.

**So the buffer at `$EF` is never filled, `kbhit()` can never return true,
and no key can ever reach the program.** The subsystem was not broken by a
coding error. It was deleted by a change of runtime, and no source file
mentions it.

### D. The path that owes nobody anything

The Plus/4 has no CIA. TED scans the keyboard itself, and the matrix can
be read directly:

```
   write the row mask (active low) to $FD30       the 6529B scan latch
   write anything to $FF08                        strobe: latch the columns
   read $FF08                                     that row's columns, active low
```

Eight rows by eight columns, `keymap[row][bit]`. This is what
`src/kbd.c` does, and it is why the fix routes *around* the operating
system rather than repairing it: nothing in the KERNAL is needed to read a
key on this machine, and depending on it was the mistake.

## IV. Method

### A. Establishing the migration baseline

Timings are taken **on the machine** against the PAL jiffy clock, both
editions measured the same way, under `xplus4` in warp with a cycle budget
and an exit screenshot.

| Primitive | BASIC 3.5 | Compiled | Speedup |
|---|---:|---:|---:|
| board draw | 23 960 ms | 123.4 ms | **194×** |
| one 4×4 button | 594 ms | 2.05 ms | **290×** |
| both token rows | 1 826 ms | 7.48 ms | **244×** |
| legal-move scan | 2 785 ms | 4.83 ms | **577×** |

Composite representative turn: 10 001 ms → 32 ms, **311×**.

These are the figures this project has; there is no aggregate 330× result,
and the honest single number for "the recoding" is **311× on a turn**,
with a per-primitive range of 194×–577×.

Binary sizes: BASIC predecessor 25 681 bytes tokenised, compiled edition
29 374 bytes — the compiled program is *larger*, and the recovered budget
was spent on animation, a raster-interrupt music engine, and this keyboard
driver.

### B. Isolating stage 1 without pressing a key

The keyboard path has two stages, and the project's own test procedure had
recorded that only the second could be tested from the host:

```
   stage 1   the operating system's interrupt scans the matrix, fills $EF
   stage 2   kbhit() reads $EF, cgetc() takes the character
```

Stage 2 was proven: VICE's `-keybuf` writes `$EF` directly and the menu
responded correctly at every delay tried. **That proof is worthless**, and
recognising why is the method: `-keybuf` writes by hand precisely the byte
stage 1 is supposed to write, so every headless keyboard test the project
could write would pass whether stage 1 worked or not.

The way round it is not to press a key. The same interrupt that scans the
matrix also advances the jiffy clock, and a clock needs no keystroke to
observe. `src/kbdiag.c` measures `TIME` across a two-second window, twice
— once with the project's own raster interrupt installed, once with it
removed — which distinguishes three diagnoses that call for opposite
fixes:

| clock, IRQ off | clock, IRQ on | meaning |
|---|---|---|
| alive | alive | the fault is above this, in the game |
| alive | **dead** | our raster interrupt is starving the KERNAL |
| **dead** | **dead** | the KERNAL interrupt never runs under this runtime |

All three bytes of the clock are watched, not one: a counter can also
appear frozen because the wrong end of it is being read.

**Measured: dead, dead.** The third row. Nothing in the project's own
interrupt code was ever implicated, and a day of searching it would have
been spent in the wrong file.

### C. Establishing the matrix decode empirically

`src/kbhunt.c` asks for one key at a time, waits for the matrix to go
quiet, then for something to go down, and records the **whole eight-byte
signature**. Measured on the machine:

| key | row | value | bit low |
|---|---:|---:|---:|
| `1` | 7 | `$FE` | 0 |
| `2` | 7 | `$F7` | 3 |
| `3` | 1 | `$FE` | 0 |
| `4` | 1 | `$F7` | 3 |
| `5` | 2 | `$FE` | 0 |
| `m` | 4 | `$EF` | 4 |

Six of six agree with the documented Plus/4 matrix, which is what licenses
`kbd.c` using the whole 64-entry table rather than only the keys that were
pressed.

## V. Results

### A. Fault 1 — the operating system is not running

Diagnosed in §IV‑B, fixed by §III‑D: `src/kbd.c` reads `$FD30`/`$FF08`
directly. `poll_key()` calls `kbd_get()` instead of `conio`.

### B. Fault 2 — a debounce counted in calls, not in time

Routing around the KERNAL did not make the menu respond. The second
defect was in the half nobody had instrumented, and it is the more
interesting of the two because the code is obviously correct in isolation.

`kbd_get()` both scanned the matrix and debounced, requiring a key to read
down on **two consecutive scans**. Nothing fixed the scan rate:

| caller | scan interval | two consecutive scans span |
|---|---|---|
| `wait_key()` — `while (!(k = poll_key ()));`, no frame wait | microseconds | ≈ 0 ms |
| `menu()` — one poll per loop; the loop is `wait_frames_live (4)` plus a lamp chase | 150–250 ms | ≈ 1/3 s |

Four orders of magnitude apart, from the same four lines. At the first
rate the debounce filters nothing, and a line caught mid-settle produces a
fresh keypress several times a second — which presented not as stray
letters but as an attract timer that never expired. At the second rate the
debounce demands the key be held for a third of a second, so an ordinary
tap is never seen twice and never arrives at all.

**One mistake, two opposite symptoms, and neither is visible in the
function that contains it.** The bug is not in `kbd_get`; it is in the
relationship between `kbd_get` and its callers.

The fix makes the rate a property of the system rather than of the caller.
`kbd_scan()` is called **once per frame from `wait_frames_live()` and from
nowhere else** — the shallow wait every part of the game already passes
through, and where the music sequencer is already serviced. `kbd_get()`
only drains a small ring the scan fills, so a key pressed during a curtain
or a piece animation survives until the game next asks. `wait_key()` waits
a frame per iteration, because a spin that never reaches
`wait_frames_live()` would never scan.

Rate: a flat 50 Hz everywhere. Debounce: 40 ms — long enough to outlast
contact bounce, short enough that no human tap can hide inside it.

### C. Implementation size

| Artefact | Size |
|---|---:|
| `src/kbd.c` | 169 lines |
| `src/kbd.h` | 53 lines |
| `kbd.o`, compiled `-Osir -Cl` | 4 723 bytes |
| `test/test_kbd.c` | 214 lines, 26 assertions |

### D. Host coverage of the decode half

`test/test_kbd.c` runs under `make check` in milliseconds. It fakes
**one** function — the eight-byte matrix read — and nothing else, which is
the only substitution §IV‑B permits: the stage it fakes is the stage
`kbhunt` measured on real silicon, not the stage under suspicion.

Covered: eleven key signatures including all of `1`–`7` and `m`; a
one-frame blip rejected; two frames accepted; a held key delivered once
and not repeated; release-and-repress delivering again; three keys
buffered across animations and drained in order; ring overflow; unmapped
keys reporting nothing; and the idle calibration in both directions.

All 26 pass.

### E. Deployment and hardware validation

The build is written to a FAT32 card with an MBR partition scheme — macOS
Disk Utility defaults to GUID, which the SD2IEC cannot read — under a
`dev/` directory, with every filename lowercase and ≤ 16 characters. That
is not cosmetic: the SD2IEC converts FAT names from ASCII to PETSCII, and
uppercase ASCII returns as glyphs that are hard to read and hard to type
on a machine that boots in upper/graphics mode. Names beyond 16 characters
fall back to unpredictable DOS 8.3 short names. `COPYFILE_DISABLE=1`
suppresses macOS AppleDouble companions, which would otherwise appear as
junk entries in a `DIRECTORY` listing.

The `card` target verifies every copy with `cmp` after `sync` and refuses
to report success otherwise.

On the machine, device 8:

```
dload"urfinkel"
run
```

**Result, on hardware, 2026‑08‑07: the program loads and runs, and the
keyboard works.** Menu selection, bed cycling with `m`, the pip and piece
prompts, and the three-press `x` exit were all exercised by hand. No
defect was observed.

This is the project's first hardware validation of any kind; every other
number in this report and its companions is from emulation.

## VI. Discussion

### A. The portability boundary was not where it was drawn

`GET` and `kbd_get()` have the same signature, the same contract and the
same failure mode when there is nothing to return. Everything about the
call site is portable. What was not portable is the answer to *who is
running the machine* — and that is not expressed anywhere in either
program.

The migration plan treated input as solved because the language offered a
primitive for it. The primitive existed; the runtime beneath it did not.
A checklist that asks "is there an equivalent API?" cannot catch this. One
that asks "what maintains the state this API reads, and is it still
there?" catches it immediately.

### B. What a host test may fake

The strongest methodological result here is a negative one. Faking `$EF`
made every keyboard test pass while the keyboard was dead, because `$EF`
was the thing under test. Faking the matrix *read* is legitimate for
`test_kbd.c` because that stage was independently measured on hardware by
`kbhunt`, and the stage under test — decode, debounce, buffering — is pure
logic downstream of it.

The rule that falls out: **a test may fake only what some other instrument
has already established.** A fake standing in for an unmeasured stage does
not reduce uncertainty, it hides it, and it produces the most dangerous
result available — a confident pass.

### C. Two faults, and why the first hid the second

Fault 1 made fault 2 unobservable. While no key could arrive at all,
nothing could distinguish "the replacement scanner has a rate bug" from
"there is still no keyboard". They were found in sequence, a day apart,
and the second only became visible once the first was gone. A subsystem
with two independent faults will present as one, and will appear to be
"still broken" after a correct fix.

## VII. Threats to validity

- **One hardware run.** §V‑E is a single session on a single restored
  machine. It establishes that the path works; it does not establish
  reliability, and it does not cover the `kbd_init` hazard below.
- **The idle calibration is untested against a real fault.** `kbd_init`
  samples the matrix once at boot and permanently ignores any line already
  low. Under emulation the idle matrix is a clean eight × `255`, so the
  calibration masks nothing and its failure mode has never been exercised.
  On a real keyboard with a leaky line it would remove keys *by column* —
  a distinctive signature, and a silent one.
- **Timing figures are PAL-specific**, 50 Hz. The 40 ms debounce is two
  frames; on an NTSC machine it would be 33 ms.
- **No SHIFT and no auto-repeat.** Names arrive lower case, which the
  renderer wanted anyway. Held keys do not repeat.
- **The speedup figures are not this report's own work** — they are
  reproduced from `lab-report.md` §VI to establish context, and are
  measured on the same emulator rather than on hardware.

## VIII. Conclusion

A keyboard is the least interesting subsystem in a program until it is the
only one that does not work. This one failed for a reason with no
representation in the source: the migration changed which software was
running the machine, and the input primitive being called depended on
software that stopped. It then failed a second time for a reason with no
representation in the function that contained it: a debounce measured in
calls, in a program where the call rate varied by four orders of magnitude
between two loops.

Both are relationship defects rather than code defects, and both were
found by measuring something adjacent — a clock rather than a keypress, a
scan rate rather than a scan — after direct testing of the thing itself
had been shown to be circular.

The result is 169 lines of C that read a 1984 keyboard matrix with no
operating system underneath, 26 host assertions holding the decode, and a
program that loads from an SD card over a serial bus into a restored
Commodore Plus/4 and answers when a key is pressed.

## References

[1] `docs/lab-report.md` — the migration as a whole; §VI for the speedup
    table, §IX‑D for the toolchain defects including the banking faults.
[2] `docs/keyboard-report.md` — the full fault trace, the C3/C4/C5
    diagnosis matrix, and the instrument failures recorded in §VI‑B.
[3] `src/kbd.c`, `src/kbd.h` — the implementation.
[4] `test/test_kbd.c` — the host-side decode tests.
[5] `src/kbhunt.c` — the matrix signature instrument (§IV‑C).
[6] `src/kbdiag.c` — the jiffy-clock instrument (§IV‑B).
[7] `src/kbtype.c` — the interactive probe: `kbd_get`'s output over the
    raw matrix rows.
[8] cc65 2.19, `asminc/plus4.inc` — `KEY_COUNT := $EF`,
    `TED_KBD := $FF08`, `TIME := $A3`.
[9] `../../docs/05-sd2iec-primer.md` — SD2IEC operation, card preparation
    and the power-path analysis for the 264 series.
[10] `urroyal.bas`, the BASIC edition — the prototype; thirteen `GET`/
    `GETKEY` sites.

---

*Released under the MIT License. Copyright (c) 2026 Paul Richeson.*
