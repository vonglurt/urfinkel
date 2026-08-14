# UR FINKEL — Music

Two voices, one volume that never moves, and a small program that runs
between the raster lines.

---

## 1. What the machine actually gives you

Before any design, the constraint, because it decides everything after it:

| | TED |
|---|---|
| Tone voices | **2** (voice 2 can be noise instead) |
| Volume | **1, global, 4 bits** — shared by both voices |
| Frequency | 10 bits per voice |
| Envelopes | none |
| Filters | none |

**There is no per-channel volume.** A quiet pad underneath a louder melody
is not something this machine can be made to do — turning the pad down
turns the melody down with it.

Three things follow, and all three are in the engine:

1. **Chords have to be arpeggiated.** Two voices cannot state a triad, so
   a voice states it one note at a time, fast enough that the ear fuses
   the sequence. This is why the interrupt runs faster than the frame.
2. **Both voices do it.** Voice 1 carries a sixteen-note figure; voice 2
   carries a held line under it. Both are built from the same chord.
3. **The volume register is furniture, not an instrument.** See §2a. This
   is the single largest change from the first version of this engine, and
   it was arrived at the hard way.

## 2. Three clocks, and a tempo that keeps them together

```
   raster interrupt   200 Hz      irq.s     writes registers
   frame sequencer     50 Hz      music.c   the grid, the parts, the level
   the bar            160 frames  music.c   the chord, the figure, the duel
```

The interrupt fires four times a frame, at raster lines 20, 98, 176 and
254. Every tick it advances each voice's figure by at most one step and
writes the two frequency registers and the volume. It makes no decisions
and touches no C.

**It is assembly, and that is not a preference.** cc65 compiles to a
software stack with shared zero-page temporaries, so calling a C function
from an interrupt corrupts whatever C was doing in the main line. The
handler has its own byte of scratch in `.bss` rather than borrowing
cc65's `tmp1`.

### 2.0 The tempo is chosen arithmetically, not by ear

There are two clocks — frames at 50 Hz and interrupt ticks at 200 Hz — and
a tempo is only usable here if a beat is a whole number of **both**:

```
   beat = 40 frames = 160 ticks   ->  50*60/40 = 75 bpm exactly
   16th = 10 frames =  40 ticks
   bar  = 160 frames = 640 ticks  =  3.2 s
```

**75 bpm is chosen for that reason and not for taste.** Every subdivision
down to a sixteenth lands on an exact frame *and* an exact tick, so the
figure and the harmony cannot slide against one another however long the
machine runs. 60, 75, 100 and 150 bpm all work; 90 and 110 do not, and
would drift apart slowly and inaudibly until something sounded wrong for
no visible reason.

### 2a. The volume register does not move

TED has no hardware envelope and one volume register for both voices. The
first version of this engine treated that as an opportunity and did three
things with it, and **all three had to be taken back out**:

| What it did | Why it went |
|---|---|
| an attack/decay envelope struck on the beat | one register, so a note's swell is also the other voice's swell |
| a **hurdy-gurdy buzz** — rhythmic attenuation over a drone | the whole texture pumps, not just the part meant to |
| **effects seizing the level** for priority | a capture ducked the entire bed to announce itself |

A second attempt kept only a per-note decay, on the reasoning that "only
reduce" was still safe shaping. It is not: a note's decay is equally the
other voice's decay, and what you hear is both voices breathing together
on every note. **There is no amount of level movement on this chip that
affects one voice only.**

So the register is written once with `VOL_BED` (2, near the bottom of the
useful 0–8 range) and stays there. Expression lives entirely in **pitch**
and in **rhythm**.

### 2b. Rests, and why they needed hardware support

A voice that can only change note has no rhythm in it — it is one
continuous sound whose pitch moves. Stopping a voice cannot be done with
the volume, because the volume is global.

Each step of each figure therefore carries a **gate**: a byte that is
non-zero to sound and zero to rest. A rest clears that voice's **enable
bit** for the tick. Both gate tables carry a rest at the same two steps —
the last sixteenth of each half-bar — so **both voices stop on the same
tick**, twice a bar.

That shared stop is what gives the bar a shape. It also forced a design
change worth recording: the held voice is written across all sixteen steps
too, the same pitch repeated (inaudible as a re-trigger), because a voice
expressed as a single sustained step has nowhere to *put* a rest at step 7.
Both voices now step at the same rate over the same sixteen positions, so
their rests cannot drift apart.

## 3. The generated bed

`music_ambient()` starts a bed that is composed rather than written.

### 3.1 The bar is the unit

Everything harmonic happens on the downbeat and nowhere else:

- the chord for this bar is taken from the progression;
- the bass lands on the **root — the lowest note of the chord**, which is
  what makes a bar audible as a bar;
- both figures are rebuilt and **restarted from step 0 in the same
  interrupt tick**, so the two voices state the new chord together rather
  than drifting into it as their own counters happen to wrap.

### 3.2 The figure is a pendulum, not a staircase

Sixteen notes across the bar. **Every other note is the base note:**

```
   root  a   root  b   root  c   root  d  …
```

The first version walked upward — chord tone, the scale tone above it, the
*next* chord tone, the scale tone above that — so every step went up and
the figure climbed out of its own register, arriving home only because the
bar ended. That is a staircase, and a staircase has no home in it.

Returning to the root every other note does two things: it keeps the root
sounding through the whole bar, reinforcing the bass, and it makes the
excursions read as ornament hung off a fixed point rather than a scale
being climbed.

The excursions alternate between a **chord tone** and the **scale tone
above it**, so half are consonances and half are the passing notes between
them. Scale tones, not arbitrary semitones — that is what makes the
in-between notes sound like ornament rather than error.

Step 14 is the last that sounds (15 rests), and it is a root, so the bar
closes on the base note and the next downbeat restates it.

### 3.3 The held line

Voice 2 is the sustained part: the root for the first half of the bar and
a second chord tone for the second half — two half notes under sixteen
sixteenths, an octave below the figure.

**The downbeat is always the root**, whatever the bank says; that is not
something a bank is allowed to elaborate away. The secondary bank chooses
the *second* half note only, so the four banks are four elaborations of one
bass line rather than four different bass lines.

### 3.4 The duel

The figure changes hands **every two bars**: one voice runs the sixteenths
while the other holds beneath it, then they swap. The subject is the same
either way — the same bank elaborating the same progression — so what the
ear follows is one line being passed between two hands.

### 3.5 The long structure

| Every | What happens |
|---|---|
| bar | the chord moves along the progression |
| 2 bars | the figure changes hands |
| phrase (the progression once) | each instrument may change bank; the key rises **a fifth** |
| 2 phrases | the mode flips between major and minor thirds |

The key rising a fifth per phrase is what walks the piece around the
**circle of fifths** rather than restating one key. The two bank rates are
notated (§4.1a) and kept coprime, so the pairing of primary against
secondary takes twelve phrases to come round again.

There is **no phrase arch**. An earlier version bent the tempo across each
phrase; a subject that keeps changing speed cannot be recognised when it
returns, which works directly against the repetition the bed is built on.
`div` in `songs.mml` is consequently unused by the bed at present.

### 3.6 The rhythm section — and the sparkle that used to be here

The secondary voice does not hold any more. It **strikes** the bar's own
bass on 1, on 3, and on the and-of-3, and rests for the other thirteen
sixteenths:

```
    step   0 . . . | 4 . . . | 8 . 10. | 12. . .
    hit    X       |         | X   X   |
    beat   1       | 2       | 3   3&  | 4
```

The rests are the instrument. A note that stops is what makes the next one
an attack, and a rest costs nothing here because the gate table clears the
voice's enable bit for that tick rather than touching the one global level.
Striking a bass note and letting it fall silent is the nearest thing TED
has to a drum without giving up a pitch.

It is 1, 3, 3& rather than the backbeat on 2 and 4 for a hardware reason:
a snare wants noise, noise is voice 2's *alternative* rather than an
addition, and this voice is carrying the whole bass line. So it plays the
figure a kick drum plays under a rock bar, not the one a snare plays over
it. Real TED noise percussion is still open as backlog 11.8.

**What was here before**, and why it went: a *sparkle* — a single high
chord tone from `SPARK_MIDI` (D5) plus the key, plus the root, plus up to
a fifth, spaced at Fibonacci intervals with two decaying echoes behind it.
Two things were wrong with it. It sat near the ceiling of what TED will
sound, so it dominated a texture it was meant to punctuate; and it was
fired through `sfx`, which **pre-empts voice 1** — so every sparkle punched
a hole in the melody in order to play a high note over it. Fibonacci
spacing made it deliberately arrhythmic, which is right for punctuation and
exactly wrong underneath a pulse.


## 4. The written songs, and the transcribed beds

Two sources feed one table, and they are different kinds of thing.

**`tools/songs.mml` is hand-written**, and holds only the **cues**: the
victory `theme` (which is the frozen BASIC edition's note for note and must
not move), the `fanfare` under the curtain, the two turn `cheer`s, the
`intro` trumpets, the `select` chime and the `rising` riser. These are short,
they are locked to animations, and they are written by hand because they
have to hit a frame.

**The beds are transcribed from `assets/midi/`** by `tools/midibed.py`. They
are **transcriptions, not arrangements**: every source is a piece for two
instruments, TED has exactly two voices, so v1 is the first part and v2 the
second and both keep their own part at its own note values.

> An earlier version of this section described a "picking cell" that laid
> each melody four notes to a bar over an invented accompaniment. That was
> replaced. It is a way of writing a bed, but it is not a transcription — it
> discards the rhythm, which in a duet is most of the composition.

### 4.0 The timing, which is the part that has to be got right

The player ticks once per PAL frame, 50 a second. A quarter note at these
tempi is almost never a whole number of frames — 96 bpm is 31.25 of them —
so durations cannot be rounded one at a time. Rounding each note
independently makes the error a random walk: over four hundred notes a piece
drifts audibly, and the two voices drift **apart**, which is worse, because
the player loops each of them separately.

So the rounding is done on **absolute onsets** and never on durations:

```
onset_frame(n) = round(seconds_at(n) * 50)
duration(n)    = onset_frame(n+1) - onset_frame(n)
```

Every note is then within half a frame of where the score puts it however
many notes precede it, and both voices are quantised against the same grid
rather than against each other. Tempo changes are integrated segment by
segment rather than assumed away — several of these pieces slow at the
close, and a single-tempo conversion puts the last bar in the wrong place.

### 4.0a The packed format, and why it is not the obvious one

A voice used to be three bytes an event — frequency low, frequency high,
duration. That is two bytes of pitch for a value with only 73 possibilities,
and the transcriptions made it expensive: they run to eight hundred events a
voice where an arrangement ran to forty.

An event is now **one byte**, an index into a per-voice **dictionary** of
`(note, duration)` pairs. Three schemes were measured over the 23 sources:

| scheme | bytes/event |
|---|---|
| plain two-byte `(note, ticks)` | 2.00 |
| run-length on the duration | **2.19** — *worse* |
| dictionary | **1.32** |

The run-length idea — emit a duration only when it changes — is the one that
looks obviously right and is not. These pieces are dotted, ornamented and
full of triplets, so the duration changes on nearly every note and the "set
duration" opcode fires almost every time, adding a byte instead of saving
one. It is recorded here so nobody re-derives it.

Every voice is encoded, **decoded back and compared** on every build
(`check_roundtrip` in `mml.py`, which mirrors `song_frame` in `music.c` line
for line). A dictionary index off by one produces music that is merely
*wrong* rather than silent, and nobody can hear a wrong byte in a
screenshot.

### 4.0b The budget — it fills the ROM and then stops

`MIDBUDGET` in the Makefile caps transcribed song data. `midibed.py` packs
sources in the order given until one will not fit, then **backfills** the
leftover budget with the cheapest of the ones it passed — a big piece early
on should not shut out three small ones behind it. Anything still dropped is
**named in the build log with its size**, so adding a `.mid` never silently
overflows the machine.

`make music-budget` prints what the last link actually left free, which is
the only honest input to `MIDBUDGET`. At the time of writing **21 of 23
sources fit**, with 557 bytes to spare; `saint-saens-fossils` (1275) and
`weber-sicilian-knight` (891) are the two that do not.

The bed list is **generated** into `src/song_beds.h` as an X-macro, so
`music.c` takes the song ids and `game.c` takes the menu labels from one
list and neither keeps a copy to forget to update.

### 4.0c The rotation order, which is a decision

`tools/bed-order.txt` is one song name per line, in the order the player
hears them, and `midibed.py` sorts its sources by it before anything else
happens. **The first line is the song the game opens on** — `music.c` starts
at `BED_FIRST_SONG`, so the rotation is running from the first bar of the
first match without anybody pressing `m`.

It exists because the Makefile hands `midibed.py` a `$(wildcard)`, which
expands in filename order. That made the rotation alphabetical *by accident*,
and it undid any deliberate ordering on the next `make music`. The list is
kept by song name rather than by filename so that renaming a source does not
silently reshuffle the game, and it is reviewable without `assets/midi`,
which is not published here.

The list does not decide what is transcribed — every `.mid` is still
converted and still measured against `MIDBUDGET`. A source it does not name
plays after the ones it does, in filename order, and a name with no source is
reported the same way; both go in the build log rather than passing quietly.

### 4.0 On the two Hurrian readings

The tablet from Ugarit (c. 1400 BCE) is the oldest complete composition we
have, and it notates **interval names in cuneiform**, not pitches — so
every modern rendering is an interpretation. Two are included because they
disagree, and the disagreement is the interesting part.

Measured across Kilmer, Dumbrill and West:

- **The scale is unanimous.** All three use `c d e f g a b` and nothing
  else.
- **The tunes are not.** Compared as melodic contour they agree 18%, 31%
  and 35% of the time, which is chance. Their openings diverge at once.

So there is no common melody to extract, and none is claimed — that would
be fabricating scholarship rather than finding it. What is shared is the
seven notes and the interval vocabulary.

Kilmer's reading needed no harmony written for it: **all 166 of its onsets
are dyads**, so v1 takes the upper note and v2 the lower. Its intervals,
counted: 66 fourths, 50 sixths, 48 thirds, 2 fifths — **no seconds and no
sevenths anywhere**, which is the same consonance rule `theme` was written
under, arrived at independently about three and a half thousand years
earlier.

### 4.1 The notation — a tracker on a beat grid

Songs are text. They are compiled on the host by `tools/mml.py` into
`src/song.h`, which is a header of `const` tables, so a song costs nothing
at run time but the bytes of its notes — there is no parser on the Plus/4
and there is no file to load.

The notation is a **tracker**: a grid of rows, one row per subdivision of a
beat, with one column per voice. Time runs down the page. This is the whole
of it:

```
song march
  tempo 24            ; frames per quarter note
  grid  4             ; rows per quarter: 4 = sixteenths
  meter 4             ; quarters in a bar
  bar
    1    g4    d3
    2    a4
    2e   b4
    2&   b4
    2a   a4
    3    b4    a3
    4    a4
    4e   g4
    4&   f4
    4a   =
```

#### The header

| | |
|---|---|
| `tempo <n>` | frames in a **quarter note**. The machine runs at 50 frames a second, so `tempo 24` is 24/50 s a beat — 125 bpm. `3000 / tempo` is the bpm. |
| `grid <n>` | rows per quarter: **1**, **2** or **4**. One row per beat, per eighth, or per sixteenth. |
| `meter <n>` | quarters in a bar. Defaults to 4. |

`tempo` must divide by `grid`, or a row would be a fraction of a frame and
the compiler says so.

#### Positions

A row starts with **where it is**, written the way it is counted aloud:

```
   grid 4     1   1e  1&  1a   2   2e  2&  2a   3  ...
              |   e   and  a   |   e   and  a   |
   grid 2     1       1&       2       2&       3  ...
   grid 1     1                2                3  ...
```

`1` is the downbeat, `1e` the second sixteenth, `1&` the third — the "and"
— and `1a` the fourth. Asking for `2e` in a song with `grid 2` is an error
rather than a rounding: that grid has no such position, and quietly moving
the note somewhere near it is how a notation starts lying to you.

**The position is stated, not counted.** That is the reason for the format.
In a stream of notes-with-durations, one wrong length shifts everything
after it and the damage shows up bars later; here an edit to bar 9 cannot
move bar 10, because bar 10 says where it is.

#### Cells

One cell per voice, `v1` then `v2`:

| cell | means |
|---|---|
| `g4`, `f#4`, `bb3` | **strike** this note now |
| `-` | **hold** — whatever is sounding keeps sounding |
| `=` | **rest** — silence from here |
| *missing* | the same as `-` |

A cell carries no length. **A note lasts until the next event on its own
voice** — the next strike or the next rest. That is what a tracker means,
and it is why a voice that changes once a bar is written once a bar rather
than padded with holds.

Rows where nothing happens on either voice may be left out entirely, for
the same reason. Write the rows that are events; the grid fills the rest.

#### `end`

```
  bar
    1    =     a3
    end  2
```

A song may stop part way through a bar, and most of these do — they are
ancient monophonic melodies and almost none of them ends on a bar line.
`end <position>` says where the last row is.

It has to be said explicitly, because a trailing hold and a finished song
look identical: both are "no further event on this voice". Without `end`
the last note of every piece that does not fill its final bar quietly
grows out to the bar line. That is not a hypothetical — it is what the
first conversion of this file did, and it was caught by the fact that the
compiled output stopped matching.

#### What it compiles to

Nothing clever. The player reads triples of *(pitch low, pitch high,
frames)*, which is a run-length encoding of exactly this grid:

```
    1    g4        g4 sounds for 4 rows      ->   g4, 4 * 6 frames
    1e   -
    1&   -
    1a   -
    2    a4                                  ->   a4, ...
```

So the grid costs nothing at run time and the player did not change when
this notation arrived. `make music` rebuilds; the compiler reports each
song's note count and the bed's pairing period.

#### The older free-duration form still exists

```
song fanfare
  v1  d5:24 e5:12 f5:24 | e5:12 d5:36
  v2  f4:24 g4:12 a4:24 | c5:12 b4:36
```

Notes with explicit lengths in frames, no grid. It is kept for exactly one
song — **the fanfare**, whose note lengths are 12, 25 and 60 frames and
share no common unit. There is no grid it sits on, because it is a flourish
rather than a measured phrase, and forcing it onto one would mean changing
it. A notation that cannot express something should say so rather than
round it.

The compiler decides which form a song is written in by whether it declares
a `grid`, so the two live in one file and a song can be moved from one to
the other on its own.

#### The conversion was proved, not trusted

Every song here except the fanfare was moved onto the grid mechanically,
and the test was that **`src/song.h` came out byte-identical** — same
notes, same durations, same rests, same order. The notation is a different
way of writing the same music, and that is checkable rather than a claim.

### 4.1a The bed, notated

The generated bed's harmony is written in the same file. It is data, not
code:

```
progression  i bVII IV v i bVII bIII v

bank primary a  1 3 5          up 0   div 7
bank second  a  1 1

rate primary 1
rate second  3
```

| Token | Meaning |
|---|---|
| `progression <chords>` | the chord per bar, as roman numerals or plain semitones. Quality is deliberately **not** written — the bed flips major/minor itself, per section |
| `bank <who> <letter> <degrees…>` | `<who>` is `primary` or `second`; `<letter>` is `a`–`d` |
| `1 3 5 8 9` | chord **degrees** — root, third, fifth, octave, ninth |
| `up <n>` | semitones added to the whole figure |
| `div <n>` | ticks per step. **Currently unused by the bed** — see §3.5 |
| `rate <who> <phrases>` | how many phrases that instrument holds a bank for |

**Degrees rather than pitches is the load-bearing choice.** A bank can only
name notes that are in the bar's chord, so no pairing of banks can be
dissonant — consonance is a property of the notation, not something the
engine checks. A secondary bank takes exactly two degrees, which is what
makes it a held harmony rather than a second melody.

**Keep the two rates coprime.** 1 against 3 gives twelve phrases; 2 against
4 collapses to eight. The compiler computes it and prints it, so the
consequence of an edit is visible at `make music` time rather than eight
minutes into a match.

### 4.2 Pitch

```
N = 1024 − 110841 / Hz          (PAL)
```

The same formula the frozen BASIC edition used, so the two editions are in
tune with each other. `N` must fit in ten bits, which puts a hard floor at
about **108 Hz — roughly A2**. Notes below it are lifted by octaves until
they fit and the compiler reports which.

### 4.3 The authoring loop

```sh
$EDITOR tools/songs.mml
make music          # -> src/song.h and src/song_ids.h
make                # relink
```

The player never changes. `song.h` defines the tables and so may be
included exactly once, by `music.c`; `song_ids.h` carries only the names
and is what the controller includes.

## 5. Effects

Effects pre-empt **voice 1** for a fixed number of ticks, so a capture or a
bear-off punches a hole in the figure rather than fighting for a channel
there isn't. They no longer touch the volume — that was how they used to be
given priority, and it ducked the whole bed to announce one event.

Eight of them: the lot striking the floor, the lot settling, the count
being read, a capture, a rosette, a piece coming home, an invalid key, and
one square of travel.

## 5a. One hardware trap, recorded because it cost a day

**Do not read-modify-write `$FF12`.** It carries voice 1's top two
frequency bits in 0–1 *and the character generator's configuration in the
rest*, so the obvious `lda / and / ora / sta` is a chance, 200 times a
second, to latch a bad read into the character base. The symptom is the
whole screen turning to garbage glyphs several minutes into a game, which
reads as a renderer bug and is not. `irq.s` snapshots the upper bits once
at install and merges against that shadow.

Note that `$FF10` — voice 2's high bits — has no such passenger, and is
written whole.

## 6. What is not built

- **No percussion.** TED's noise mode is voice 2's alternative, and voice 2
  is carrying the held line.
- **No mute persistence.** The bed choice is not remembered across a boot,
  because there is nothing to remember it in.
- **The bed does not react to the match.** Tension — a piece one square
  from home, a capture threatened — could pick the bank or the mode. It
  does not.
- **`div` is notated but unused** (§3.5).
- **West's reading** of the Hurrian tablet is parsed and measured but not
  included; it is largely monody in a lower register and would duplicate
  what Dumbrill's already does.

## 7. Where it lives

| File | What |
|---|---|
| `src/irq.s` | the raster interrupt: both figures, the gates, the volume |
| `src/music.c` | the sequencer: musical time, the bar, the beds, effects |
| `src/music.h` | the public face, and the constraint stated up front |
| `tools/songs.mml` | the songs and the bed's harmony, as text |
| `tools/mml.py` | the compiler: pitch table, songs, banks, progression |
| `src/song.h` | generated: note table and song data (music.c only) |
| `src/song_ids.h` | generated: the song names, safe to include anywhere |

---

*Released under the MIT License. Copyright (c) 2026 Paul Richeson.*
