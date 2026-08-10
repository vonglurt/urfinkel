#!/usr/bin/env python3
"""Measure the songs in songs.mml, so a claim about them can be checked.

WHAT THIS IS FOR NOW, WHICH IS LESS THAN IT WAS.  It was written to judge
a set of ARRANGEMENTS - beds laid on a repeating "picking cell" - and that
design was replaced by faithful transcriptions from assets/midi, so two of
its columns now measure a thing that no longer exists.  Read it with that
in mind:

  STILL USEFUL
    DRIFT     v1's total length against v2's.  This is a real invariant
              and it applies to every song in the file: the player loops
              each voice independently when IT reaches its own end, so
              two voices of unequal total length slide a little further
              apart on every repeat.  A song flagged DRIFT is a bug.
    DENSITY   onsets a second - still the honest measure of how busy a
              cue is, and cues have to sit under animation.
    HARMONY   the consonant share.  Worth policing in the hand-written
              cues; NOT worth policing in a transcription, where the
              intervals are the composer's and not ours to correct.

  MEASURES AN ABANDONED DESIGN
    PINCH     how often the two voices strike together.  Meaningful only
              for the picking cell, where it was the definition.
    CELL      whether every bar shares one onset pattern.  Real music
              does not, and should not - a transcription scoring low
              here is behaving correctly.

The round-trip check that actually guards the song data lives in mml.py
(check_roundtrip) and runs on every build.  This is a reading instrument,
not a gate.

An onset here is a pitch CHANGE, because TED has no envelope: striking
the same pitch twice writes the register the value it already holds and
nothing happens at the speaker.

The original four quantities, kept because the reasoning still explains
what the columns mean:

  DENSITY   onsets per second, both voices.  This is "frantic".  A note
            here is a pitch CHANGE, because TED has no envelope: striking
            the same pitch twice is inaudible, so restatements do not
            count as onsets and the ear's rate is the change rate.

  HARMONY   the vertical interval between the voices, sampled at every
            point where either voice moves.  Reported as the share that
            are consonant - unison, third, fourth, fifth, sixth, octave -
            against seconds, sevenths and the tritone.  This is the rule
            songs.mml already states in its header, measured rather than
            asserted.

  PINCH     the share of v2's onsets that land on the same tick as a v1
            onset.  Fingerstyle is defined by the thumb and the fingers
            agreeing on the beat and disagreeing off it, so this wants to
            be high while density stays low - the two together are what
            "synchronised movement" means.

  PULSE     the share of v1 onsets that fall on a beat, and the number of
            distinct note lengths in use.  A stream of one length has no
            metre in it however fast it goes; a small vocabulary of
            lengths, most of them landing on beats, is a rhythm.

Usage:  mmlstat.py songs.mml [name ...] [--detail]

--detail lists every moment the two voices are a second, a seventh or a
tritone apart, located as bar and beat, so a clash can be gone to and
fixed rather than merely counted.
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mml

FPS = 50.0

CONSONANT = {0, 3, 4, 5, 7, 8, 9}       # unison 3rd 4th 5th 6th octave


def events(pairs):
    """[(midi|None, ticks)] -> [(start_tick, midi|None)], plus total."""
    out, t = [], 0
    for midi, ticks in pairs:
        out.append((t, midi))
        t += ticks
    return out, t


def sounding(ev, total, t):
    """The pitch sounding on a voice at tick t, or None."""
    cur = None
    for start, midi in ev:
        if start > t:
            break
        cur = midi
    return cur


def onsets(ev):
    """Ticks where the voice CHANGES pitch - the audible attacks.

    A repeat of the same pitch is dropped: with no envelope the register
    is rewritten with the value it already held and nothing happens at
    the speaker.  A rest counts as a change (the voice is cut), and the
    note after a rest counts too."""
    out, prev = [], "start"
    for start, midi in ev:
        if midi != prev:
            out.append((start, midi))
        prev = midi
    return out


NAMES = ["c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"]


def spell(midi):
    return "%s%d" % (NAMES[midi % 12], midi // 12 - 1)


def clashes(voices, tempo, meter=4):
    """Every moment the voices are a 2nd, 7th or tritone apart.

    Located as bar and beat rather than as a tick, because that is how the
    source is written and so that is where the fix has to be typed."""
    e1, t1 = events(voices[1])
    e2, t2 = events(voices[2])
    out = []
    for t in sorted(set([x for x, _ in e1] + [x for x, _ in e2])):
        a, b = sounding(e1, t1, t), sounding(e2, t2, t)
        if a is None or b is None:
            continue
        iv = abs(a - b) % 12
        if iv in CONSONANT:
            continue
        if tempo:
            bar = t // (tempo * meter) + 1
            beat = (t % (tempo * meter)) / float(tempo) + 1
            where = "bar %2d beat %4.2f" % (bar, beat)
        else:
            where = "tick %d" % t
        out.append((where, spell(a), spell(b), iv))
    return out


def analyse(name, voices, tempo, grid, meter=4):
    e1, t1 = events(voices[1])
    e2, t2 = events(voices[2])
    total = max(t1, t2)
    secs = total / FPS

    o1 = [t for t, m in onsets(e1) if m is not None]
    o2 = [t for t, m in onsets(e2) if m is not None]
    dens = (len(o1) + len(o2)) / secs if secs else 0.0

    # Harmony: sample wherever either voice moves.
    marks = sorted(set([t for t, _ in e1] + [t for t, _ in e2]))
    cons = diss = 0
    for t in marks:
        a, b = sounding(e1, t1, t), sounding(e2, t2, t)
        if a is None or b is None:
            continue
        if abs(a - b) % 12 in CONSONANT:
            cons += 1
        else:
            diss += 1
    harm = 100.0 * cons / (cons + diss) if cons + diss else 100.0

    # Pinch: v2 attacks that coincide with a v1 attack.
    s1 = set(o1)
    pinch = sum(1 for t in o2 if t in s1)
    pinchpc = 100.0 * pinch / len(o2) if o2 else 0.0

    # Pulse: v1 attacks on a beat, and how many distinct lengths exist.
    beat = tempo if tempo else None
    if beat:
        onbeat = sum(1 for t in o1 if t % beat == 0)
        beatpc = 100.0 * onbeat / len(o1) if o1 else 0.0
    else:
        beatpc = float("nan")
    lens = sorted(set(tk for _m, tk in voices[1] if tk))

    # Cell: do the bars share ONE onset pattern?  A rhythm is a figure
    # heard again; a stream of notes that never lays out the same way
    # twice has a tempo but no groove, however even it is.
    cell = float("nan")
    if beat:
        barlen = beat * meter
        shape = {}
        for t in o1 + o2:
            shape.setdefault(t // barlen, []).append(t % barlen)
        if shape:
            pats = ["|".join(str(x) for x in sorted(set(v)))
                    for v in shape.values()]
            cell = 100.0 * max(pats.count(p) for p in set(pats)) / len(pats)

    drift = t1 - t2
    return dict(name=name, secs=secs, n1=len(o1), n2=len(o2), dens=dens,
                harm=harm, pinch=pinchpc, beat=beatpc, lens=lens, drift=drift,
                cell=cell)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    with open(sys.argv[1]) as f:
        text = f.read()
    argv = [a for a in sys.argv[2:] if a != "--detail"]
    detail = "--detail" in sys.argv
    want = set(argv)

    # The parser drops tempo/grid, and the pulse figure needs them, so
    # read them back off the source.  Cheap, and keeps mml.py unchanged.
    meta, cur = {}, None
    for line in text.splitlines():
        tok = line.split(";", 1)[0].split()
        if not tok:
            continue
        if tok[0].lower() == "song" and len(tok) > 1:
            cur = tok[1]
            meta[cur] = {}
        elif cur and tok[0].lower() in ("tempo", "grid", "meter") and len(tok) > 1:
            if tok[1].isdigit():
                meta[cur][tok[0].lower()] = int(tok[1])

    songs, *_ = mml.parse(text)
    rows, bad = [], []
    for name, voices in songs:
        if want and name not in want:
            continue
        m = meta.get(name, {})
        rows.append(analyse(name, voices, m.get("tempo"), m.get("grid"),
                            m.get("meter", 4)))
        if detail:
            for c in clashes(voices, m.get("tempo"), m.get("meter", 4)):
                bad.append((name,) + c)

    print("%-11s %6s %5s %5s %7s %7s %7s %7s %7s  %s"
          % ("song", "secs", "v1", "v2", "onset/s", "conson", "pinch",
             "on-beat", "cell", "lengths"))
    print("-" * 96)
    for r in rows:
        print("%-11s %6.1f %5d %5d %7.2f %6.0f%% %6.0f%% %6.0f%% %6.0f%%  %s%s"
              % (r["name"], r["secs"], r["n1"], r["n2"], r["dens"],
                 r["harm"], r["pinch"], r["beat"], r["cell"],
                 ",".join(str(x) for x in r["lens"][:6]),
                 " DRIFT %+d" % r["drift"] if r["drift"] else ""))
    if detail:
        print("\nclashes - a 2nd, 7th or tritone between the voices:")
        if not bad:
            print("  none")
        for name, where, a, b, iv in bad:
            print("  %-10s %-18s v1 %-4s over v2 %-4s  (%d semitones)"
                  % (name, where, a, b, iv))
    return 0


if __name__ == "__main__":
    sys.exit(main())
