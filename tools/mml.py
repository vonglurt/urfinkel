#!/usr/bin/env python3
"""Compile the UR FINKEL music notation into src/song.h.

The notation is a plain text string, which is the whole point: songs are
written and edited as text on the host, and the player never changes.

There are two notations here and a song may be written in either.

THE TRACKER, which is what everything except the fanfare uses: a grid of
rows, one per subdivision of a beat, one column per voice, time running
down the page.  A row says WHERE it is - the position as it is counted
aloud - so an edit to one bar cannot move the next one.

    song march
      tempo 24                  frames per quarter note (50 fps: 125 bpm)
      grid  4                   rows per quarter - 1, 2 or 4
      meter 4                   quarters in a bar
      bar
        1    g4    d3           strike both voices
        2    a4                 v2 omitted: it holds
        2e   b4
        4a   =                  rest
      end  4a                   the song stops here, part way through

A cell is a pitch, `-` to hold, or `=` to rest; a missing cell holds, and
a row where nothing happens may be left out.  No cell carries a length: a
note lasts until the next event on its own voice, which run-length encodes
straight into the player's (pitch, frames) triples.

THE FREE-DURATION FORM, kept for music that does not sit on a beat:

    ; a comment
    song theme
      l24                       default note length, in ticks
      v1  d5 e5:12 f5 e5:12 | d5 c5:12 a4:12 d5:36
      v2  f4 g4:12 a4 c5:12 | b4 a4:12 f4:12 f4:36

Which one a song is in is decided by whether it declares a `grid`.  The
full reader's account is in docs/music.md section 4.1.

  ;             everything after a semicolon is a comment.  It is
                NOT "#", because "#" is a sharp.
  song <name>   start a new song
  v1 / v2       the voice the following notes belong to; TED has two
  l<n>          default note length in ticks from here on
  <note><oct>   c d e f g a b, with # or b for accidentals, octave 2-7
  :<n>          this note's length in ticks, overriding the default
  r             a rest - the voice is silenced for that long
  |             bar line, ignored, there for the eye

The generated ambient bed is notated here too.  It has two instruments -
a PRIMARY that arpeggiates and a SECONDARY that sustains - and each owns
four banks, a to d:

    progression  i bVII IV v i bVII bIII v
    bank primary a  1 3 5        up 0   div 7
    bank primary b  1 5 3 8      up 0   div 5
    bank primary c  1 3 5 8 5 3  up 12  div 3
    bank primary d  1 8          up 0   div 11
    bank second  a  1 1
    bank second  b  1 5
    bank second  c  5 5
    bank second  d  3 1
    rate primary 1
    rate second  3

  progression   the chord per bar, as roman numerals (i bII II bIII III
                IV bV V bVI VI bVII VII) or plain semitones.  Quality is
                not written: the bed decides major or minor per section.
  bank <who> <letter> <degrees...> [up <n>] [div <n>]
                <who> is "primary" or "second".  <degrees> are chord
                degrees - 1 root, 3 third, 5 fifth, 8 octave, 9 ninth -
                so a bank can only ever name notes that are in the chord,
                which is what makes the two instruments consonant by
                construction.  "up" adds semitones, "div" is the ticks
                per arpeggio step before the phrase arch bends it.
                A secondary bank takes exactly two degrees and alternates
                between them bar by bar.
  rate <who> <phrases>
                how many phrases each bank of that instrument holds for
                before the next one takes over.  The two rates are the
                bed's largest structure: if they are coprime the pairing
                of primary against secondary does not come round until
                their product of phrases has passed, which is why the
                default 1 against 3 takes twelve phrases - four banks
                times three - to repeat.

A tick is one PAL frame, 1/50 s, because that is the rate the C sequencer
runs at.  So :50 is a second and :12 is a semiquaver at 100 bpm.

Pitch becomes a TED frequency register value through

    N = 1024 - 110841 / Hz          (PAL)

which is the formula the BASIC edition used, so both editions are in tune
with each other.  N must be in 0..1023, which puts a hard floor on the
machine at about 108 Hz - roughly A2.  Notes below that are transposed up
by octaves until they fit, and the transposition is reported.
"""

import math
import re
import sys

PAL_CONST = 110841.0
NOTE_BASE_MIDI = 36                     # C2
NOTE_TOP_MIDI = 108                     # C8
SEMITONE = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def midi_to_n(midi):
    """TED frequency register value for a MIDI note, or None if too low."""
    hz = 440.0 * (2.0 ** ((midi - 69) / 12.0))
    n = round(1024 - PAL_CONST / hz)
    if n < 0 or n > 1023:
        return None
    return int(n)


def build_note_table():
    table, lifted = [], []
    for midi in range(NOTE_BASE_MIDI, NOTE_TOP_MIDI + 1):
        m, n = midi, midi_to_n(midi)
        while n is None and m < NOTE_TOP_MIDI:
            m += 12                     # too low for TED: lift an octave
            n = midi_to_n(m)
        if n is None:
            n = 0
        if m != midi:
            lifted.append((midi, m))
        table.append(n)
    return table, lifted


NOTE_RE = re.compile(r"^([a-g])([#b]?)([2-7])?(?::(\d+))?$")

BANKS = 4
BANK_MAX_STEPS = 8

# Chord degree -> index into the engine's chord_tone[] table.
DEGREE = {
    "1": 0, "root": 0,
    "3": 1, "third": 1,
    "5": 2, "fifth": 2,
    "8": 3, "oct": 3, "octave": 3,
    "9": 4, "ninth": 4,
}

ROMAN = {
    "i": 0, "bii": 1, "ii": 2, "biii": 3, "iii": 4, "iv": 5,
    "bv": 6, "v": 7, "bvi": 8, "vi": 9, "bvii": 10, "vii": 11,
}

# Used when songs.mml notates no bed of its own, so a build never breaks
# for want of a bank.
DEFAULT_PRIMARY = [
    (3, [0, 1, 2],          0,  7),
    (4, [0, 2, 1, 3],       0,  5),
    (6, [0, 1, 2, 3, 2, 1], 12, 3),
    (2, [0, 3],             0, 11),
]
DEFAULT_SECONDARY = [(0, 0), (0, 2), (2, 2), (1, 0)]
DEFAULT_PROGRESSION = [0, 10, 5, 7, 0, 10, 3, 7]

# Phrases each instrument holds a bank for.  One against three is coprime,
# so four primary banks against three phrases of secondary take twelve
# phrases - about eight minutes - to pair up the same way twice.
DEFAULT_RATES = {"primary": 1, "second": 3}


class ParseError(Exception):
    pass


# --- the packed song format ----------------------------------------------
#
# A voice used to be three bytes an event - frequency low, frequency high,
# duration - which is 2 bytes of pitch for a value that only ever takes 73
# distinct values.  The transcribed duets made that expensive: they are
# faithful to the MIDI rather than laid on a four-note grid, so they run to
# eight hundred events a voice instead of forty.
#
# So an event is now ONE byte, an index into a per-voice DICTIONARY of
# (note, duration) pairs.  Music repeats its pairs heavily - a quaver on
# the dominant recurs all through a piece - and measured over the twenty
# three sources this costs 1.32 bytes an event against the old 2.00, which
# is a third more music in the same ROM.
#
# Two other schemes were measured and rejected on the evidence:
#
#   plain two-byte (note, ticks)      2.00 bytes/event
#   run-length on the duration        2.19 bytes/event   <- WORSE
#   dictionary                        1.32 bytes/event
#
# The run-length idea - emit a duration only when it changes - is the one
# that looks obviously right and is not.  These pieces are dotted, ornamented
# and full of triplets, so the duration changes on nearly every note and the
# "set duration" opcode fires almost every time, adding a byte instead of
# saving one.  It is recorded here so nobody re-derives it.
#
#   stream byte   0xFF          end of voice
#                 0xFE          literal follows: note byte, then ticks byte
#                 else          index into the dictionary
#   dict entry    2 bytes       note index (0xFF = rest), ticks
#
# The literal escape exists because a dictionary is capped at 254 entries;
# a voice with more distinct pairs than that keeps its commonest 254 and
# spells the rest out.  In practice no source here needs it, but a piece
# that did would otherwise fail to build rather than merely encode larger.

STREAM_END = 0xFF
STREAM_LIT = 0xFE
DICT_REST = 0xFF
DICT_MAX = 254


def encode_voice(pairs):
    """[(midi|None, ticks)] -> (stream bytes, dict bytes).

    Durations are already <= 255 by construction: the tracker cannot make
    a longer one and midibed.py splits long notes into a chain at the same
    pitch, which is inaudible without an envelope."""
    ev = []
    for midi, ticks in pairs:
        t = max(1, min(255, ticks))
        n = DICT_REST if midi is None else midi - NOTE_BASE_MIDI
        if not 0 <= n <= 255:
            raise ParseError("note %r is off the table" % midi)
        ev.append((n, t))

    freq = {}
    for e in ev:
        freq[e] = freq.get(e, 0) + 1
    # Commonest first, so the ones that survive the cap are the ones that
    # pay for themselves most often.
    order = sorted(freq, key=lambda e: (-freq[e], e))[:DICT_MAX]
    slot = {e: i for i, e in enumerate(order)}

    stream = []
    for e in ev:
        if e in slot:
            stream.append(slot[e])
        else:
            stream.append(STREAM_LIT)
            stream.extend(e)
    stream.append(STREAM_END)

    table = []
    for n, t in order:
        table.extend((n, t))
    return stream, table


def decode_voice(stream, table):
    """The C player's loop, in Python, so the encoder can be checked.

    This mirrors song_frame() in music.c deliberately and line for line.
    Nobody can hear a wrong byte in a screenshot, and a dictionary index
    off by one produces music that is merely WRONG rather than silent -
    the worst failure to catch late.  So every voice is decoded back and
    compared against what went in, on every build."""
    out, i = [], 0
    while True:
        b = stream[i]
        i += 1
        if b == STREAM_END:
            return out
        if b == STREAM_LIT:
            n, t = stream[i], stream[i + 1]
            i += 2
        else:
            n, t = table[2 * b], table[2 * b + 1]
        out.append((None if n == DICT_REST else n + NOTE_BASE_MIDI, t))


def voice_cost(pairs):
    """Bytes this voice will occupy, for the packer's budget."""
    s, d = encode_voice(pairs)
    return len(s) + len(d)


def check_roundtrip(name, v, pairs):
    """Encode, decode, compare.  Raises rather than emitting bad data."""
    stream, table = encode_voice(pairs)
    got = decode_voice(stream, table)
    want = [(m, max(1, min(255, t))) for m, t in pairs]
    if got != want:
        for k, (a, b) in enumerate(zip(got, want)):
            if a != b:
                raise ParseError(
                    "%s v%d: event %d encodes as %r but should be %r"
                    % (name, v, k, a, b))
        raise ParseError("%s v%d: %d events in, %d out"
                         % (name, v, len(want), len(got)))


def pairing_period(rates):
    """Phrases before the same primary/secondary pairing comes round again.

    Each instrument walks BANKS banks holding each for its own rate, so a
    full cycle is BANKS*rate phrases and the pairing repeats at the lowest
    common multiple of the two."""
    a = BANKS * rates["primary"]
    b = BANKS * rates["second"]
    return a * b // math.gcd(a, b)


def parse_bank(tokens, lineno, primary, secondary):
    """bank <primary|second> <a-d> <degrees...> [up n] [div n]"""
    if len(tokens) < 4:
        raise ParseError("line %d: bank needs an instrument, a letter and "
                         "at least one degree" % lineno)
    who = tokens[1]
    if who not in ("primary", "second", "secondary"):
        raise ParseError("line %d: '%s' is not primary or second" % (lineno, who))
    letter = tokens[2]
    if len(letter) != 1 or not ("a" <= letter <= chr(ord("a") + BANKS - 1)):
        raise ParseError("line %d: bank letter must be a-%s"
                         % (lineno, chr(ord("a") + BANKS - 1)))
    slot = ord(letter) - ord("a")

    steps, up, div, i = [], 0, 7, 3
    while i < len(tokens):
        tok = tokens[i]
        if tok in ("up", "div"):
            if i + 1 >= len(tokens):
                raise ParseError("line %d: '%s' needs a number" % (lineno, tok))
            try:
                val = int(tokens[i + 1])
            except ValueError:
                raise ParseError("line %d: '%s' needs a number" % (lineno, tok))
            if tok == "up":
                up = val
            else:
                div = val
            i += 2
            continue
        if tok not in DEGREE:
            raise ParseError("line %d: '%s' is not a chord degree "
                             "(1 3 5 8 9)" % (lineno, tok))
        steps.append(DEGREE[tok])
        i += 1

    if not steps:
        raise ParseError("line %d: bank names no degrees" % lineno)

    if who == "primary":
        if len(steps) > BANK_MAX_STEPS:
            raise ParseError("line %d: a primary bank may have at most %d "
                             "steps" % (lineno, BANK_MAX_STEPS))
        if not 0 <= div <= 255 or not 0 <= up <= 60:
            raise ParseError("line %d: up/div out of range" % lineno)
        primary[slot] = (len(steps), steps, up, div)
    else:
        if len(steps) != 2:
            raise ParseError("line %d: a secondary bank alternates between "
                             "exactly two degrees" % lineno)
        secondary[slot] = (steps[0], steps[1])


def parse_rate(tokens, lineno, rates):
    """rate <primary|second> <phrases>

    How long a bank holds before the next one takes over.  This is the
    bed's largest structure - the thing a listener hears as "it has moved
    on" - so it belongs in the notation with the figures rather than in
    the sequencer with the plumbing."""
    if len(tokens) != 3:
        raise ParseError("line %d: rate needs an instrument and a number of "
                         "phrases" % lineno)
    who = tokens[1]
    if who not in ("primary", "second", "secondary"):
        raise ParseError("line %d: '%s' is not primary or second" % (lineno, who))
    try:
        n = int(tokens[2])
    except ValueError:
        raise ParseError("line %d: '%s' is not a number of phrases"
                         % (lineno, tokens[2]))
    # Zero would mean "change banks every no phrases at all", which the
    # sequencer counts down towards and would never reach.
    if not 1 <= n <= 255:
        raise ParseError("line %d: a rate is 1 to 255 phrases, not %d"
                         % (lineno, n))
    rates["primary" if who == "primary" else "second"] = n


def parse_progression(tokens, lineno):
    out = []
    for tok in tokens[1:]:
        if tok in ROMAN:
            out.append(ROMAN[tok])
        elif tok.isdigit() and int(tok) < 12:
            out.append(int(tok))
        else:
            raise ParseError("line %d: '%s' is not a chord degree" % (lineno, tok))
    if not out:
        raise ParseError("line %d: progression names no chords" % lineno)
    if len(out) > 32:
        raise ParseError("line %d: progression is longer than 32 bars" % lineno)
    return out


# --- the tracker ---------------------------------------------------------
#
# A grid notation, for songs whose rhythm sits on a beat.  See docs/music.md
# for the reader's version; this is the parser's.
#
#     song march
#       tempo 24        frames per quarter note (50 fps, so 24 = 125 bpm)
#       grid  4         rows per quarter: 4 = sixteenths, 2 = eighths, 1 = beats
#       meter 4         quarters in a bar
#       bar
#         1    g4   d3
#         2    a4
#         2e   b4
#         4a   =
#
# A row is a POSITION followed by one cell per voice.  The position is the
# usual spoken count - 1, 1e, 1&, 1a, 2, ... - so where a note falls is
# stated rather than inferred from what came before it, which is the whole
# reason for the format: an edit to bar 9 cannot silently move bar 10.
#
# A cell is a pitch (strike it), `-` (hold what is sounding), or `=` (rest).
# A missing cell holds, so a voice that changes once a bar is written once
# a bar.  Rows may be omitted entirely for the same reason.
#
# Duration is not written anywhere.  A note lasts until the next event on
# its own voice, which is what a tracker means and what run-length encoding
# turns back into the player's (pitch, ticks) pairs.

SUBDIV = {1: [""], 2: ["", "&"], 4: ["", "e", "&", "a"]}

HOLD, REST = "-", "="


def pos_index(pos, grid, meter, lineno):
    """'2e' -> the row number inside the bar, 0-based."""
    m = re.match(r"^(\d+)([ea&]?)$", pos)
    if not m:
        raise ParseError("line %d: '%s' is not a position like 1, 2e, 3& or 4a"
                         % (lineno, pos))
    beat, sub = int(m.group(1)), m.group(2)
    if not (1 <= beat <= meter):
        raise ParseError("line %d: beat %d is outside a %d-beat bar"
                         % (lineno, beat, meter))
    subs = SUBDIV[grid]
    if sub not in subs:
        raise ParseError("line %d: '%s' needs a grid that has it - grid %d "
                         "offers %s" % (lineno, pos, grid,
                                        " ".join(repr(x) for x in subs)))
    return (beat - 1) * grid + subs.index(sub)


def note_midi(tok, lineno):
    m = NOTE_RE.match(tok)
    if not m:
        raise ParseError("line %d: cannot read '%s'" % (lineno, tok))
    step, acc, octv, length = m.groups()
    if length:
        raise ParseError("line %d: '%s' carries a length, and a tracker row "
                         "takes its length from the grid" % (lineno, tok))
    midi = 12 * (int(octv or 4) + 1) + SEMITONE[step]
    if acc == "#":
        midi += 1
    elif acc == "b":
        midi -= 1
    if not (NOTE_BASE_MIDI <= midi <= NOTE_TOP_MIDI):
        raise ParseError("line %d: '%s' is off the table" % (lineno, tok))
    return midi


class Tracker(object):
    """Accumulates a grid song and flattens it to the player's pairs."""

    def __init__(self, name):
        self.name = name
        self.tempo = None
        self.grid = None
        self.meter = 4
        self.rows = {1: [], 2: []}      # one cell per grid row, in order
        self.bar_open = False
        self.limit = None               # the row the song stops on

    def setting(self, key, value, lineno):
        if key == "grid" and value not in SUBDIV:
            raise ParseError("line %d: grid %d - only 1, 2 or 4 rows per beat"
                             % (lineno, value))
        setattr(self, key, value)

    def open_bar(self, lineno):
        for k in ("tempo", "grid"):
            if getattr(self, k) is None:
                raise ParseError("line %d: song %s needs a %s before its first "
                                 "bar" % (lineno, self.name, k))
        if self.tempo % self.grid:
            raise ParseError("line %d: tempo %d does not divide into %d rows - "
                             "a row would be a fraction of a frame"
                             % (lineno, self.tempo, self.grid))
        for v in (1, 2):
            self.rows[v] += [HOLD] * (self.meter * self.grid)
        self.bar_open = True

    def place(self, pos, cells, lineno):
        if not self.bar_open:
            raise ParseError("line %d: '%s' outside any bar" % (lineno, pos))
        base = len(self.rows[1]) - self.meter * self.grid
        idx = base + pos_index(pos, self.grid, self.meter, lineno)
        for v, cell in enumerate(cells[:2], start=1):
            if cell in (HOLD, REST):
                self.rows[v][idx] = cell
            else:
                self.rows[v][idx] = note_midi(cell, lineno)

    def end(self, pos, lineno):
        """`end 3&` - the song stops there, part way through a bar.

        Needed because a trailing hold is indistinguishable from a song
        that has finished: both are "no further event on this voice".  A
        held final note and a song that stops are different music, and
        without this the last note of every piece that does not fill its
        last bar quietly grows to the bar line.  These pieces are ancient
        monophonic melodies and almost none of them end on a bar line."""
        if not self.bar_open:
            raise ParseError("line %d: 'end' outside any bar" % lineno)
        base = len(self.rows[1]) - self.meter * self.grid
        self.limit = base + pos_index(pos, self.grid, self.meter, lineno)

    def flatten(self, lineno):
        """Run-length encode the grid into (midi|None, ticks) pairs."""
        per = self.tempo // self.grid
        stop = len(self.rows[1]) if self.limit is None else self.limit
        out = {}
        for v in (1, 2):
            pairs, cur, run = [], None, 0
            for cell in self.rows[v][:stop]:
                if cell == HOLD:
                    run += 1
                    continue
                if run:
                    pairs.append((cur, run * per))
                cur = None if cell == REST else cell
                run = 1
            if run:
                pairs.append((cur, run * per))
            # A grid that opens on a hold has nothing to hold onto.
            if pairs and pairs[0][0] is None and self.rows[v][0] == HOLD:
                pass
            out[v] = pairs
        return out


def parse(text):
    songs = []                          # [(name, {voice: [(midi|None, ticks)]})]
    cur = None
    trk = None                          # the tracker song being read, if any
    voice = None
    deflen = 25
    lineno = 0

    primary = list(DEFAULT_PRIMARY)
    secondary = list(DEFAULT_SECONDARY)
    rates = dict(DEFAULT_RATES)
    progression = None

    for raw in text.splitlines():
        lineno += 1
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue

        tokens = line.split()
        head = tokens[0].lower()

        # --- a tracker song owns its lines -----------------------------
        # Once a song has declared a grid it is a tracker, and every line
        # until the next `song` is a row rather than a stream of notes.
        # The two notations coexist on purpose: a grid can only describe
        # music that sits on a beat, and not all of this file's does - see
        # the fanfare, whose 12, 25 and 60 tick notes share no unit.
        if trk is not None:
            if head in ("tempo", "grid", "meter"):
                if len(tokens) < 2 or not tokens[1].isdigit():
                    raise ParseError("line %d: %s needs a number"
                                     % (lineno, head))
                trk.setting(head, int(tokens[1]), lineno)
                continue
            # A song is only a TRACKER once it has a grid.  Until then it
            # might still turn out to be a free-duration song, and `v1`
            # says it is - which is what lets the two live in one file.
            if trk.grid is not None:
                if head == "bar":
                    trk.open_bar (lineno)
                    continue
                # Anything that starts a new section hands the song back;
                # everything else on these lines is a row.
                if head == "end":
                    if len(tokens) < 2:
                        raise ParseError("line %d: end needs a position"
                                         % lineno)
                    trk.end (tokens[1], lineno)
                    continue
                if head not in ("song", "bank", "rate", "progression"):
                    trk.place (tokens[0], tokens[1:], lineno)
                    continue
                cur[1].update (trk.flatten (lineno))
                trk = None
            elif head in ("v1", "v2"):
                trk = None              # free-duration after all
            elif head in ("bar", "end"):
                raise ParseError("line %d: '%s' needs a grid - a tracker song "
                                 "declares tempo and grid before its first bar"
                                 % (lineno, head))

        # The bed's directives are line-oriented: they take the whole line
        # rather than one token at a time, because a bank is a list.
        if head == "bank":
            parse_bank([t.lower() for t in tokens], lineno, primary, secondary)
            continue
        if head == "rate":
            parse_rate([t.lower() for t in tokens], lineno, rates)
            continue
        if head == "progression":
            progression = parse_progression([t.lower() for t in tokens], lineno)
            continue

        i = 0
        while i < len(tokens):
            tok = tokens[i].lower()
            i += 1

            if tok == "song":
                if i >= len(tokens):
                    raise ParseError("line %d: song needs a name" % lineno)
                cur = (tokens[i], {1: [], 2: []})
                songs.append(cur)
                voice, deflen = None, 25
                trk = Tracker(tokens[i])
                i += 1
                continue
            if tok in ("tempo", "grid", "meter"):
                raise ParseError("line %d: '%s' before any song" % (lineno, tok))
            if tok in ("v1", "v2"):
                if cur is None:
                    raise ParseError("line %d: %s before any song" % (lineno, tok))
                voice = int(tok[1])
                continue
            if tok == "|":
                continue
            if tok.startswith("l") and tok[1:].isdigit():
                deflen = int(tok[1:])
                continue

            if voice is None:
                raise ParseError("line %d: '%s' before any voice" % (lineno, tok))

            m = re.match(r"^r(?::(\d+))?$", tok)
            if m:
                cur[1][voice].append((None, int(m.group(1) or deflen)))
                continue

            m = NOTE_RE.match(tok)
            if not m:
                raise ParseError("line %d: cannot read '%s'" % (lineno, tok))
            step, acc, octv, length = m.groups()
            midi = 12 * (int(octv or 4) + 1) + SEMITONE[step]
            if acc == "#":
                midi += 1
            elif acc == "b":
                midi -= 1
            if not (NOTE_BASE_MIDI <= midi <= NOTE_TOP_MIDI):
                raise ParseError("line %d: '%s' is off the table" % (lineno, tok))
            cur[1][voice].append((midi, int(length or deflen)))

    if trk is not None and (trk.rows[1] or trk.rows[2]):
        cur[1].update(trk.flatten(lineno))

    if progression is None:
        progression = list(DEFAULT_PROGRESSION)
    return songs, primary, secondary, progression, rates


def emit(songs, table, lifted, primary, secondary, progression, rates, out):
    w = out.write
    w("/* GENERATED by tools/mml.py from tools/songs.mml - do not edit.\n"
      "**\n"
      "** Rebuild with `make music`.  Included by exactly one translation\n"
      "** unit (music.c), which is why the tables are defined and not just\n"
      "** declared here.\n"
      "**\n"
      "** Pitch is a TED frequency register value, N = 1024 - 110841/Hz\n"
      "** (PAL) - the same formula the frozen BASIC edition used, so the\n"
      "** two editions are in tune with one another.\n"
      "*/\n\n")
    w("#ifndef URFINKEL_SONG_H\n#define URFINKEL_SONG_H\n\n")
    w("#include \"song_ids.h\"\n\n")
    w("#define NOTE_BASE_MIDI %d\n" % NOTE_BASE_MIDI)
    w("#define NOTE_COUNT     %d\n\n" % len(table))

    if lifted:
        w("/* Below about 108 Hz the TED register value would go negative,\n"
          "** so these notes are lifted by an octave until they fit:\n")
        for a, b in lifted:
            w("**   midi %d -> %d\n" % (a, b))
        w("*/\n")

    for name, arr in (("note_lo", [n & 0xFF for n in table]),
                      ("note_hi", [(n >> 8) & 0x03 for n in table])):
        w("static const unsigned char %s[NOTE_COUNT] = {\n" % name)
        for i in range(0, len(arr), 12):
            w("    " + ", ".join("%3d" % v for v in arr[i:i + 12]) + ",\n")
        w("};\n\n")

    w("/* A voice is a STREAM of one-byte dictionary indices, and a\n"
      "** DICTIONARY of (note, ticks) pairs it indexes into.  See the\n"
      "** commentary above encode_voice() in tools/mml.py for why, and for\n"
      "** the two schemes that were measured and rejected.\n"
      "**\n"
      "**   stream  255  end of voice\n"
      "**           254  literal follows: a note byte, then a ticks byte\n"
      "**           else index into this voice's dictionary\n"
      "**   dict    two bytes per entry: note index (255 = rest), ticks\n"
      "**\n"
      "** A note index is an offset from NOTE_BASE_MIDI into note_lo/hi\n"
      "** above, so a pitch costs one byte here rather than two. */\n")
    w("#define SONG_END   255\n"
      "#define SONG_LIT   254\n"
      "#define SONG_REST  255\n\n")

    uniq, total = [], 0
    for name, voices in songs:
        if name not in uniq:
            uniq.append(name)
        for v in (1, 2):
            check_roundtrip(name, v, voices[v])
            stream, dic = encode_voice(voices[v])
            total += len(stream) + len(dic)
            w("static const unsigned char song_%s_k%d[] = {\n" % (name, v))
            for i in range(0, len(dic), 16):
                w("    " + ", ".join("%3d" % x for x in dic[i:i + 16]) + ",\n")
            w("};\n")
            w("static const unsigned char song_%s_v%d[] = {\n" % (name, v))
            for i in range(0, len(stream), 16):
                w("    " + ", ".join("%3d" % x for x in stream[i:i + 16]) + ",\n")
            w("};\n\n")

    for v in (1, 2):
        for kind in ("v", "k"):
            w("static const unsigned char* const song_%s%d[SONG_COUNT] = {\n"
              % (kind, v))
            for name in uniq:
                w("    song_%s_%s%d,\n" % (name, kind, v))
            w("};\n\n")

    w("/* %d bytes of song data in all. */\n\n" % total)

    w("""
/* --- the generated ambient bed ----------------------------------------
** Two instruments - a primary that arpeggiates, a secondary that sustains
** - each with four banks.  Every step is a CHORD DEGREE rather than a
** pitch, so both voices can only ever name notes that are in the bar's
** chord, and no pairing of banks can be dissonant. */
""")
    w("#define BANKS           %d\n" % BANKS)
    w("#define BANK_MAX_STEPS  %d\n\n" % BANK_MAX_STEPS)
    w("struct bank {\n"
      "    unsigned char len;                      /* steps in the figure  */\n"
      "    unsigned char step[BANK_MAX_STEPS];     /* chord-tone indices   */\n"
      "    unsigned char oct;                      /* semitones added      */\n"
      "    unsigned char div;                      /* ticks per arp step   */\n"
      "};\n\n")

    w("static const struct bank primary[BANKS] = {\n")
    for n, steps, up, div in primary:
        pad = list(steps) + [0] * (BANK_MAX_STEPS - len(steps))
        w("    { %d, {%s}, %2d, %2d },\n"
          % (n, ",".join(str(v) for v in pad), up, div))
    w("};\n\n")

    w("/* Two degrees each: a secondary bank alternates between them bar by\n"
      "** bar, which is how a hurdy-gurdy's two drone strings behave. */\n")
    w("static const unsigned char secondary[BANKS][2] = {\n")
    for a, b in secondary:
        w("    { %d, %d },\n" % (a, b))
    w("};\n\n")

    w("#define PROGRESSION_BARS %d\n" % len(progression))
    w("static const unsigned char progression[PROGRESSION_BARS] = {\n    "
      + ", ".join(str(v) for v in progression) + "\n};\n\n")

    w("/* Phrases each instrument holds a bank for.  Coprime rates are the\n"
      "** point: %d against %d takes %d phrases to pair the same two banks\n"
      "** twice, which is the bed's longest structure. */\n"
      % (rates["primary"], rates["second"], pairing_period(rates)))
    w("#define PRIM_EVERY      %d\n" % rates["primary"])
    w("#define SEC_EVERY       %d\n\n" % rates["second"])

    w("#endif\n")


def emit_ids(songs, out):
    """The song identifiers, in a header safe to include anywhere.

    The tables themselves are defined rather than declared, so song.h can
    only be included once; the controller needs the names and not the
    notes, and this is what it includes."""
    w = out.write
    w("/* GENERATED by tools/mml.py from tools/songs.mml - do not edit.\n"
      "** Rebuild with `make music`.  Identifiers only: the note tables are\n"
      "** in song.h, which only music.c may include. */\n\n")
    w("#ifndef URFINKEL_SONG_IDS_H\n#define URFINKEL_SONG_IDS_H\n\n")
    w("#define SONG_COUNT %d\n" % len(songs))
    for i, (name, _v) in enumerate(songs):
        w("#define SONG_%-10s %d\n" % (name.upper(), i))
    w("\n#endif\n")


def main():
    # The last argument is the output; everything before it is a source, so
    # the hand-written cues and the machine-written duets can live in
    # separate files and still compile into one table.
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    srcs, dst = sys.argv[1:-1], sys.argv[-1]
    text = ""
    for src in srcs:
        with open(src) as f:
            text += f.read() + "\n"
    try:
        songs, primary, secondary, progression, rates = parse(text)
    except ParseError as e:
        print("mml: %s" % e, file=sys.stderr)
        return 1
    if not songs:
        print("mml: %s define no songs" % " ".join(srcs), file=sys.stderr)
        return 1

    table, lifted = build_note_table()
    with open(dst, "w") as f:
        emit(songs, table, lifted, primary, secondary, progression, rates, f)
    ids = dst.replace(".h", "_ids.h")
    with open(ids, "w") as f:
        emit_ids(songs, f)

    grand = 0
    for name, voices in songs:
        b = voice_cost(voices[1]) + voice_cost(voices[2])
        grand += b
        print("mml: %-14s v1 %4d  v2 %4d  %5d bytes"
              % (name, len(voices[1]), len(voices[2]), b))
    print("mml: %-14s %26d bytes of song data" % ("TOTAL", grand))
    print("mml: bed        %d bars, primary %s, secondary %s"
          % (len(progression),
             "/".join(str(b[0]) for b in primary),
             "/".join("%d-%d" % s for s in secondary)))
    print("mml: rates      primary every %d phrase(s), second every %d - "
          "the pairing repeats after %d"
          % (rates["primary"], rates["second"], pairing_period(rates)))
    print("mml: wrote %s and %s" % (dst, ids))
    return 0


if __name__ == "__main__":
    sys.exit(main())
