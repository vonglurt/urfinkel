#!/usr/bin/env python3
"""Build the animation gallery: every effect, caught mid-motion, on one sheet.

Each effect is a separate binary (src/anim.c, -DANIM=n) running that one
thing on a loop against a plain background.  This builds them all, shoots
each at a cycle count chosen to land INSIDE the motion rather than at
either end of it, and lays the results out as a contact sheet.

Why a separate binary each: a screenshot of an effect embedded in the game
is a screenshot of the game, and what it caught depends on the match being
played.  One effect alone, looping, with nothing else on screen, is
reproducible - shoot it twice and get the same picture.

Two shots per effect, a little apart, because a still frame cannot show
whether something is moving.  The pair is the evidence.

    make anim
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build")
BREW = os.environ.get("BREW_PREFIX", "/opt/homebrew")

CORE = ["board.c", "rules.c", "text.c", "dice.c", "urbot.c", "music.c",
        "front.c", "kbd.c", "etch.c", "dbg.c", "blit.s", "irq.s"]

# n, title, and the two cycle counts to shoot at.  The counts are chosen
# per effect because they run at wildly different lengths - the trophy is
# poured over four seconds, the turn sweep is over in under one.
#
# NOTHING BEFORE ABOUT 110e6, AND THAT NUMBER MOVES WITH THE BINARY.  A
# shot before the PRG has finished loading catches the LOADING screen and
# viceshot rejects it, which is what a "?" in the run output means.  These
# were written when the binaries were thirty kilobytes and the threshold
# was 60e6; the transcribed beds took them past fifty-five kilobytes and
# the whole sheet quietly became twelve pictures of a loader - every row
# "?" - until 2026-08-08.  Measured then by bisection on anim2, which
# draws the board the instant it starts: still loading at 105e6, running
# at 110e6.  If it happens again the fix is here and not in the effects -
# bisect the same way and move every number below by the difference.
#
# The second shot of each pair is deliberately far from the first, because
# a still cannot show motion and the pair is the evidence.
SHOTS = [
    (0, "laser etch - text",      (119_000_000, 123_000_000)),
    (1, "laser etch - big UR",    (115_000_000, 125_000_000)),
    (2, "shimmer - orbiting",     (120_000_000, 121_500_000)),
    (3, "turn sweep",             (118_000_000, 119_500_000)),
    (4, "the lots tumble",        (119_000_000, 121_000_000)),
    (5, "curtain and lamps",      (120_000_000, 135_000_000)),
    (6, "the trophy",             (115_000_000, 125_000_000)),
    (7, "piece glide",            (118_000_000, 119_000_000)),
    (8, "menu scroll-in",         (115_000_000, 121_000_000)),
    # The fireworks only start after the name has been cut, which is 90
    # frames in - hence much later than the rest.
    (9, "fireworks",              (262_600_000, 263_400_000)),
    # The two that take the whole apron.  Both are long - a capture runs
    # 190 frames and a block-letter cut 200 - so the pair is shot wide
    # apart: early enough to catch the fire building and the first letters
    # being cut, late enough to catch the blaze and the finished word.
    (10, "burn - the apron alight", (150_000_000, 200_000_000)),
    (11, "block letters - the win", (175_000_000, 190_000_000)),
    # Not one effect but the whole end of a match, in victory()'s own
    # order and with its own numbers.  What goes wrong at the end of a
    # match is BETWEEN the effects - the cup landing on the name, the wipe
    # eating the two lines above it, the border left on a firework's hue -
    # and no single-effect entry can show any of that.
    (12, "the end of a match",     (185_000_000, 235_000_000)),
]


def build(n):
    prg = os.path.join(BUILD, "anim%d.prg" % n)
    cmd = ([os.path.join(BREW, "bin", "cl65"), "-t", "plus4", "-Osir", "-Cl",
            "-DBUILD_DATE=\"gallery\"", "-DANIM=%d" % n, "-o", prg,
            os.path.join(ROOT, "src", "anim.c")]
           + [os.path.join(ROOT, "src", f) for f in CORE])
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        print(r.stdout + r.stderr, file=sys.stderr)
        raise SystemExit("anim%d failed to build" % n)
    return prg


def shoot(prg, png, cycles):
    r = subprocess.run(
        [sys.executable, os.path.join(HERE, "viceshot.py"), prg, png,
         str(cycles), "--max-white=0.25"],
        capture_output=True, text=True, cwd=ROOT,
        env=dict(os.environ, BREW_PREFIX=BREW),
    )
    return r.returncode == 0 and os.path.exists(png)


def sheet(rows):
    from PIL import Image, ImageDraw
    if not rows:
        raise SystemExit("nothing shot")
    w, h = Image.open(rows[0][1]).size
    scale = 2
    tw, th = w // scale, h // scale
    pad, lab = 6, 14
    cols = 2
    n = len(rows)
    gw = cols * (tw * 2 + pad * 3)
    gh = ((n + cols - 1) // cols) * (th + lab + pad) + pad
    out = Image.new("RGB", (gw, gh), (24, 24, 28))
    d = ImageDraw.Draw(out)
    for i, (title, a, b) in enumerate(rows):
        cx = (i % cols) * (tw * 2 + pad * 3) + pad
        cy = (i // cols) * (th + lab + pad) + pad
        d.text((cx, cy), title, fill=(230, 230, 210))
        for j, p in enumerate((a, b)):
            im = Image.open(p).convert("RGB").resize((tw, th), Image.NEAREST)
            out.paste(im, (cx + j * (tw + pad), cy + lab))
    path = os.path.join(BUILD, "anim-sheet.png")
    out.save(path)
    return path


def main():
    os.makedirs(BUILD, exist_ok=True)
    rows = []
    for n, title, (c1, c2) in SHOTS:
        prg = build(n)
        a = os.path.join(BUILD, "anim%d-a.png" % n)
        b = os.path.join(BUILD, "anim%d-b.png" % n)
        ok_a, ok_b = shoot(prg, a, c1), shoot(prg, b, c2)
        moving = "?"
        if ok_a and ok_b:
            moving = "moving" if open(a, "rb").read() != open(b, "rb").read() \
                     else "STILL"
        print("  %d  %-24s %s" % (n, title, moving))
        if ok_a and ok_b:
            rows.append(("%d  %s  (%s)" % (n, title, moving), a, b))
    print()
    print("sheet:", sheet(rows))


if __name__ == "__main__":
    main()
