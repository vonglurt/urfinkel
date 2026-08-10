#!/usr/bin/env python3
"""Convert flute duets from MIDI into UR FINKEL songs, faithfully.

WHY THIS EXISTS.  The first attempt at these pieces laid their melodies
four notes to a bar over an invented accompaniment.  That is a way of
writing a bed, but it is not a transcription: it throws away the rhythm,
which in a duet is most of the composition.  Every source here is a piece
for TWO instruments and TED has exactly two voices, so the honest mapping
is the obvious one - v1 is the first part, v2 is the second, and both keep
their own note values.

THE TIMING, which is the part that has to be got right.

The player ticks once per PAL frame, 50 a second.  A quarter note at the
sources' tempi is almost never a whole number of frames - 96 bpm is 31.25
of them - so durations cannot simply be rounded one at a time.  Rounding
each note independently makes the error a random walk: over four hundred
notes a piece drifts audibly, and the two voices drift APART, which is
worse, because the player loops each of them separately.

So the rounding is done on ABSOLUTE ONSETS and never on durations:

    onset_frame(n) = round(seconds_at(n) * 50)
    duration(n)    = onset_frame(n+1) - onset_frame(n)

Every note is then within half a frame of where the score puts it, no
matter how many notes precede it, and the two voices are quantised
against the same grid rather than against each other.  This is the same
trick as error diffusion in a dither: the residue is carried, not
discarded.

Tempo changes are honoured by integrating the tempo map segment by
segment rather than assuming one tempo throughout - several of these
pieces have a rallentando at the close, and a single-tempo conversion
puts the last bar in the wrong place.

WHAT IT EMITS is free-duration MML, which tools/mml.py already parses, so
this adds a front end and changes no back end.
"""

import struct
import sys
import os

FPS = 50.0
NOTE_BASE_MIDI = 36                     # matches mml.py
NOTE_TOP_MIDI = 107                     # NOTE_RE only spells octaves 2-7
NAMES = ["c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"]

# One event costs two bytes in the packed format: a note-table index and a
# duration in ticks.  Two terminators per song, one per voice.
BYTES_PER_EVENT = 2
BYTES_PER_SONG_OVERHEAD = 2 * BYTES_PER_EVENT + 4   # ends + two table slots


class Mid(object):
    def __init__(self, path):
        d = open(path, "rb").read()
        if d[:4] != b"MThd":
            raise ValueError("%s is not a MIDI file" % path)
        hlen = struct.unpack(">I", d[4:8])[0]
        self.fmt, self.ntrk, self.div = struct.unpack(">HHH", d[8:14])
        if self.div & 0x8000:
            raise ValueError("SMPTE division is not handled")
        p, self.tracks = 8 + hlen, []
        while p < len(d) and d[p:p + 4] == b"MTrk":
            ln = struct.unpack(">I", d[p + 4:p + 8])[0]
            self.tracks.append(d[p + 8:p + 8 + ln])
            p += 8 + ln


def vlq(b, i):
    v = 0
    while True:
        v = (v << 7) | (b[i] & 0x7F)
        c = b[i] & 0x80
        i += 1
        if not c:
            return v, i


def events(trk):
    """[(abs_tick, kind, a, b)] for one track; kind is 'on','off','meta'."""
    i, t, out, run = 0, 0, [], None
    while i < len(trk):
        dt, i = vlq(trk, i)
        t += dt
        if trk[i] & 0x80:
            st = trk[i]
            i += 1
            run = st
        else:
            st = run
        if st == 0xFF:
            mt = trk[i]
            i += 1
            ln, i = vlq(trk, i)
            out.append((t, "meta", mt, trk[i:i + ln]))
            i += ln
        elif st in (0xF0, 0xF7):
            ln, i = vlq(trk, i)
            i += ln
        else:
            hi = st & 0xF0
            if hi in (0xC0, 0xD0):
                i += 1
            else:
                a, b = trk[i], trk[i + 1]
                i += 2
                if hi == 0x90 and b:
                    out.append((t, "on", a, b))
                elif hi in (0x80, 0x90):
                    out.append((t, "off", a, 0))
    return out


def tempo_map(mid):
    """[(abs_tick, us_per_quarter)], always starting at tick 0."""
    tm = []
    for trk in mid.tracks:
        for t, kind, a, data in events(trk):
            if kind == "meta" and a == 0x51:
                tm.append((t, int.from_bytes(data, "big")))
    tm.sort()
    if not tm or tm[0][0] != 0:
        tm.insert(0, (0, 500000))       # MIDI default, 120 bpm
    return tm


def meta_of(mid):
    """Time signature and key, for the comment the caller writes."""
    sig, key = None, None
    for trk in mid.tracks:
        for t, kind, a, data in events(trk):
            if kind == "meta" and a == 0x58 and sig is None:
                sig = (data[0], 2 ** data[1])
            elif kind == "meta" and a == 0x59 and key is None:
                key = (struct.unpack("b", data[:1])[0], data[1])
    return sig, key


class Clock(object):
    """Absolute MIDI tick -> frame, integrating the tempo map.

    Kept as a running list of (tick, seconds_at_that_tick, us_per_quarter)
    so a lookup is a walk over a handful of segments rather than a sum over
    every event."""

    def __init__(self, tm, div):
        self.div, self.seg = div, []
        secs = 0.0
        for i, (tick, us) in enumerate(tm):
            if i:
                ptick, pus = tm[i - 1]
                secs += (tick - ptick) / div * (pus / 1e6)
            self.seg.append((tick, secs, us))

    def frame(self, tick):
        lo = self.seg[0]
        for s in self.seg:
            if s[0] > tick:
                break
            lo = s
        secs = lo[1] + (tick - lo[0]) / self.div * (lo[2] / 1e6)
        return int(round(secs * FPS))


def voice(trk, clock):
    """One track -> [(frame, midi|None)], monophonic, rests included.

    Where the part is chordal the TOP note is taken: these are duets, so a
    double stop is an editorial nicety and the upper line is the part."""
    ev = events(trk)
    on = {}                             # pitch -> onset tick
    notes = []                          # (start_tick, end_tick, pitch)
    for t, kind, a, b in ev:
        if kind == "on":
            on[a] = t
        elif kind == "off" and a in on:
            notes.append((on.pop(a), t, a))
    for a, t in on.items():
        notes.append((t, t, a))
    if not notes:
        return []
    notes.sort()

    # Collapse simultaneities to the highest note, then walk in time.
    out, i = [], 0
    while i < len(notes):
        st = notes[i][0]
        group = [n for n in notes if n[0] == st]
        i += len(group)
        top = max(group, key=lambda n: n[2])
        out.append(top)

    seq = []
    for k, (st, en, pitch) in enumerate(out):
        seq.append((clock.frame(st), pitch))
        nxt = out[k + 1][0] if k + 1 < len(out) else None
        if nxt is None or en < nxt:
            seq.append((clock.frame(en), None))     # a real rest
    return seq


def spell(midi):
    while midi > NOTE_TOP_MIDI:
        midi -= 12
    while midi < NOTE_BASE_MIDI:
        midi += 12
    return "%s%d" % (NAMES[midi % 12], midi // 12 - 1)


def pack(seq, stop):
    """[(frame, midi|None)] -> [(token, ticks)], durations from onsets.

    A duration over 255 does not fit the tick byte, so it is split into a
    chain of events at the same pitch.  That is free here and inaudible:
    with no envelope, restriking a pitch writes the register the value it
    already holds and nothing happens at the speaker."""
    out = []
    for k, (f, midi) in enumerate(seq):
        end = seq[k + 1][0] if k + 1 < len(seq) else stop
        d = end - f
        if d <= 0:
            continue
        tok = "r" if midi is None else spell(midi)
        while d > 255:
            out.append((tok, 255))
            d -= 255
        out.append((tok, d))
    return out


def convert(path):
    """-> (name, v1_events, v2_events, info)"""
    mid = Mid(path)
    clock = Clock(tempo_map(mid), mid.div)
    sig, key = meta_of(mid)

    parts = [(n, voice(t, clock)) for n, t in enumerate(mid.tracks)]
    parts = [(n, s) for n, s in parts if len([x for x in s if x[1]]) > 4]
    parts.sort(key=lambda ns: -len(ns[1]))
    parts = parts[:2]
    if not parts:
        raise ValueError("%s has no playable part" % path)
    parts.sort(key=lambda ns: ns[0])            # keep score order

    end = max(s[-1][0] for _n, s in parts)
    v = [pack(s, end) for _n, s in parts]
    if len(v) == 1:
        v.append([("r", 255)] * (end // 255) + [("r", end % 255 or 1)])

    # Both voices must total the same, or they drift a little more on
    # every loop - the player loops each one when IT reaches its own end.
    for k in (0, 1):
        tot = sum(d for _t, d in v[k])
        if tot < end:
            v[k].append(("r", min(255, end - tot)))

    tempo0 = tempo_map(mid)[0][1]
    info = dict(secs=end / FPS, div=mid.div,
                bpm=6e7 / tempo0, sig=sig, key=key,
                n1=len(v[0]), n2=len(v[1]),
                bytes=(len(v[0]) + len(v[1])) * BYTES_PER_EVENT
                      + BYTES_PER_SONG_OVERHEAD)
    return info, v


def emit(name, v, out):
    out.write("song %s\n" % name)
    for k, label in ((0, "v1"), (1, "v2")):
        out.write("  %s\n" % label)
        line = "   "
        for tok, d in v[k]:
            piece = " %s:%d" % (tok, d)
            if len(line) + len(piece) > 76:
                out.write(line + "\n")
                line = "   "
            line += piece
        if line.strip():
            out.write(line + "\n")


def songname(path):
    """A C identifier, and a short one: it also names the menu entry."""
    b = os.path.basename(path).lower()
    for suf in (".mid", ".midi"):
        if b.endswith(suf):
            b = b[:-len(suf)]
    out = "".join(c if c.isalnum() else "_" for c in b)
    while "__" in out:
        out = out.replace("__", "_")
    return out.strip("_")


# The menu line is "m  music: " plus the name inside a 28-column stage, so
# a name has 18 characters and the ordinal eats two or three of them.
NAME_MAX = 18

# Words that identify nothing once the composer is known.  Dropping them is
# what makes room for the part that tells two pieces by one composer apart.
NOISE = set("""for two flutes flute duet duo duos sonata in the of no op
               and a school grade concerts etudes divertimentos faciles
               major minor sharp mignonnes epitaph
               i ii iii iv v vi vii viii ix x xi xii
               le la les du des con brio""".split())


def display(name, ordinal):
    """A menu label: the composer, then whatever distinguishes the piece."""
    parts = [p for p in name.split("_") if p]
    composer = parts[0]
    # Single letters are key names ("in g major") once "major" has gone,
    # and a bare "g" on a menu line identifies nothing.
    tail = [p for p in parts[1:] if p not in NOISE and len(p) > 1]

    # Prefer words to opus numbers - "rondo" tells a player more than
    # "op46" does - but keep a number if nothing else survived.
    words = [p for p in tail if not any(c.isdigit() for c in p)]
    nums = [p for p in tail if any(c.isdigit() for c in p)]
    tail = (words + nums) if words else nums

    lead = "%d " % ordinal
    room = NAME_MAX - len(lead)
    out = composer[:room]
    for t in tail:
        if len(out) + 1 + len(t) > room:
            # Truncating the last word beats dropping it: "divertimen" still
            # identifies the piece, an empty tail does not.
            if room - len(out) > 4:
                out += " " + t[:room - len(out) - 1]
            break
        out += " " + t
    return lead + out


def real_cost(v):
    """Exactly what this song will occupy, asked of the real encoder.

    Not an estimate: the packer's whole job is to stop at the last song
    that fits, and a guess that runs 5% light overflows the machine at
    link time instead of dropping one piece here."""
    import mml
    tot = 0
    for voice in v:
        pairs = []
        for tok, d in voice:
            if tok == "r":
                pairs.append((None, d))
            else:
                m = mml.NOTE_RE.match(tok)
                step, acc, octv, _l = m.groups()
                midi = 12 * (int(octv) + 1) + mml.SEMITONE[step]
                if acc == "#":
                    midi += 1
                elif acc == "b":
                    midi -= 1
                pairs.append((midi, d))
        tot += mml.voice_cost(pairs)
    return tot + 4                      # two slots in the pointer tables


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    budget = None
    out = None
    for a in sys.argv[1:]:
        if a.startswith("--budget="):
            budget = int(a.split("=", 1)[1])
        elif a.startswith("--out="):
            out = a.split("=", 1)[1]
    if not args:
        print(__doc__)
        return 2

    print("%-40s %6s %6s %5s %5s %6s %s"
          % ("piece", "secs", "bpm", "v1", "v2", "bytes", "sig"))
    print("-" * 92)

    kept, spent, dropped = [], 0, []
    for p in args:
        try:
            info, v = convert(p)
            cost = real_cost(v)
        except Exception as e:                  # noqa: BLE001 - reported
            print("%-40s  FAILED: %s" % (os.path.basename(p)[:40], e))
            continue
        name = songname(p)
        sig = "%d/%d" % info["sig"] if info["sig"] else "-"
        # STOP AT THE FIRST ONE THAT DOES NOT FIT, and keep going: a big
        # piece near the front should not shut out three small ones behind
        # it, and the caller asked for as much music as will go.
        if budget is not None and spent + cost > budget:
            dropped.append((name, cost))
            print("%-40s %6.1f %6.1f %5d %5d %6d %s   DROPPED - %d over"
                  % (name[:40], info["secs"], info["bpm"], info["n1"],
                     info["n2"], cost, sig, spent + cost - budget))
            continue
        spent += cost
        kept.append((name, v, info))
        print("%-40s %6.1f %6.1f %5d %5d %6d %s"
              % (name[:40], info["secs"], info["bpm"], info["n1"],
                 info["n2"], cost, sig))

    # BACKFILL, smallest first.  The pass above walks the sources in the
    # order given, which is right - a deliberate ordering should be honoured
    # - but it means one big piece early on can shut out several small ones
    # behind it, and "as much music as fits" is a count and not a byte
    # total.  So whatever budget is left over is offered to the pieces that
    # were passed, cheapest first, until none of them fits either.
    if budget is not None and dropped:
        for name, cost in sorted(dropped, key=lambda nc: nc[1]):
            if spent + cost > budget:
                continue
            src = [p for p in args if songname(p) == name][0]
            info, v = convert(src)
            spent += cost
            kept.append((name, v, info))
            dropped = [d for d in dropped if d[0] != name]
            print("%-40s %6.1f %6.1f %5d %5d %6d     BACKFILLED"
                  % (name[:40], info["secs"], info["bpm"],
                     info["n1"], info["n2"], cost))
        # Keep the emitted order the same as the order asked for, so the
        # rotation is not reshuffled by which pieces happened to backfill.
        order = {songname(p): i for i, p in enumerate(args)}
        kept.sort(key=lambda k: order[k[0]])

    print("-" * 92)
    print("kept %d song(s), %d bytes%s"
          % (len(kept), spent,
             " of a %d budget (%d spare)" % (budget, budget - spent)
             if budget is not None else ""))
    if dropped:
        print("dropped %d, none of which fits the %d spare: %s"
              % (len(dropped), budget - spent,
                 ", ".join("%s (%d)" % (n, c) for n, c in dropped)))

    beds = None
    for a in sys.argv[1:]:
        if a.startswith("--beds="):
            beds = a.split("=", 1)[1]
    if beds:
        # An X-macro, so the identifier list and the label list cannot drift
        # apart: music.c expands it one way for the song ids and game.c the
        # other for the menu, and neither has a table of its own to forget.
        with open(beds, "w") as f:
            f.write("/* GENERATED by tools/midibed.py - do not edit.\n"
                    "** Rebuild with `make music`.\n"
                    "**\n"
                    "** The written beds, in rotation order.  Expand"
                    " BED_SONG_LIST with an\n"
                    "** X(id, label) macro: music.c takes the ids, game.c"
                    " takes the labels,\n"
                    "** and there is no second table to keep in step. */\n\n")
            f.write("#ifndef URFINKEL_SONG_BEDS_H\n"
                    "#define URFINKEL_SONG_BEDS_H\n\n")
            f.write("#include \"song_ids.h\"\n\n")
            f.write("#define BED_SONG_COUNT %d\n\n" % len(kept))
            f.write("#define BED_SONG_LIST \\\n")
            for i, (name, _v, _info) in enumerate(kept, start=1):
                label = display(name, i)
                if len(label) > NAME_MAX:
                    raise SystemExit("label %r is %d chars, over %d"
                                     % (label, len(label), NAME_MAX))
                f.write("    X(SONG_%-28s \"%s\") \\\n"
                        % (name.upper() + ",", label))
            f.write("    /* end */\n\n#endif\n")
        print("wrote %s (%d beds)" % (beds, len(kept)))
        for i, (name, _v, _i) in enumerate(kept, start=1):
            print("   %2d %-18s %s" % (i, display(name, i), name))

    if out:
        with open(out, "w") as f:
            f.write("; GENERATED by tools/midibed.py from assets/midi -"
                    " do not edit.\n"
                    "; Rebuild with `make music`.  These are transcriptions:"
                    " both voices\n"
                    "; carry their own part at its own note values, and the"
                    " timing is\n"
                    "; quantised on absolute onsets so nothing drifts.\n\n")
            for name, v, info in kept:
                sig = "%d/%d" % info["sig"] if info["sig"] else "?"
                f.write("; %s - %s, %.0f bpm, %.0f seconds\n"
                        % (name, sig, info["bpm"], info["secs"]))
                emit(name, v, f)
                f.write("\n")
        print("wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
