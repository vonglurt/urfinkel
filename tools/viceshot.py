#!/usr/bin/env python3
"""Run a Plus/4 PRG headless under VICE and screenshot the result.

VICE's autostart is not deterministic.  Roughly one run in eight it loses
the RUN keystroke after LOADING and leaves the machine sitting at READY
with the program loaded but never started; the screenshot is then the
BASIC boot screen.  Measured over 8 runs per configuration:

    plain -autostart                        6/8
    -autostartprgmode 1 (inject)            0/8
    -autostartwithcolon + fixed delay       7/8
    +autostart-warp + fixed delay           5/5

No setting makes it certain, so this harness verifies the result instead
of hoping for it, and retries when the run did not take.

There are two predicates, and which one to use depends on whether the
program under test holds still.

    --min-black   the default.  Every program we screenshot ends with the
                  TED background set to true black - the playfield's own
                  colour, which UR ROYAL settles on so that reverse-video
                  glyphs render black.  The BASIC boot screen is pale
                  violet on white and has essentially no black in it.

    --max-white   for a program that is still PLAYING when the shot is
                  taken.  The black test asks the frame to be a particular
                  colour, and a live match is legitimately not: turn_sweep
                  floods the background for 72 frames every time the
                  players change, so a shot that lands inside one is a
                  healthy game that the black test throws away.  Retrying
                  does not help, because VICE under -warp is deterministic
                  enough that every attempt lands on the same frame.

                  So test for the failure itself instead of for a proxy of
                  success: the BASIC boot screen is 57% pure white and no
                  screenshot this project takes of a running program is
                  over 3%.

    --still-at    a SECOND cycle count.  The program is shot twice and the
                  two frames must DIFFER, which is the only way to catch a
                  machine that has stopped: a wedged game board is still a
                  game board and passes every predicate above.  This is not
                  hypothetical - the smoke test passed for hours on a board
                  that had been frozen since 360e6 cycles, because nothing
                  was asking whether anything was still moving.

                  IT IS NOT ENOUGH ON ITS OWN, and the second finding is
                  more interesting than the first.  UR FINKEL's shimmer
                  animates the tiles from the raster interrupt whatever the
                  game is doing, so "something changed" stays true over a
                  game that has stopped progressing.  A soak found the demo
                  dead-ended on `press space` at the end of a match, and the
                  only cells differing across three hundred million cycles
                  were the shimmer's.

                  So the comparison IGNORES the rows the shimmer owns - the
                  top and bottom row of each band of tiles - and the border,
                  which carries the current player's colour.  What is left
                  is the board's contents, the pieces, the casting floor
                  and the chronicle, and if none of those has moved then
                  the game is not progressing whatever the decoration is
                  doing.  --shimmer-rows overrides the list.

                  WHAT THIS TEST CAN AND CANNOT DO.  The two frames come
                  from two SEPARATE emulator runs, and the autostart delay
                  at the top of this file is not perfectly repeatable - a
                  run that starts a frame later plays a different match,
                  because rnd_stir folds the raster into the seed.  So:

                    it fires  ->  believe it.  Two runs of the same binary
                                  that reach identical pictures at
                                  different cycle counts are not a game in
                                  progress.
                    it passes ->  believe it less.  A shifted start can
                                  produce two different matches and hide a
                                  stall that is really there.

                  It is a one-sided test and is worth having as one.  The
                  reliable form is a soak: the same binary shot at four or
                  five cycle depths, and built at several ATTRACT_FRAMES so
                  the matches genuinely differ.  That is how both the wedge
                  and the trophy dead-end were actually found.

    usage: viceshot.py <prg> <png> [cycles] [--min-black F] [--max-white F]
                       [--still-at CYCLES]
"""

import subprocess
import sys
import os

MIN_BLACK = 0.15        # fraction of the frame that must be pure black
ATTEMPTS = 6

# Character rows the shimmer animates: the highlight and shadow row of each
# of the three bands of tiles, with the board's bands at 0, 5 and 10.  They
# are excluded from the "is it still moving" comparison because they move
# whether or not the game does.  See --still-at.
SHIMMER_ROWS = (0, 3, 5, 8, 10, 13)
CHAR_H = 8              # pixels per character row
SCREEN_W = 320          # the displayed picture, inside the border
SCREEN_H = 200

VICE_FLAGS = [
    "-default",                 # ignore saved settings, so runs reproduce
    "-warp",
    "-sounddev", "dummy",
    "-autostartwithcolon",      # 'RUN:' rather than 'RUN' - measurably better
    "-autostart-delay", "4",
    "+autostart-delay-random",  # a random delay is a random result
]


def colour_fraction(path, rgb):
    from PIL import Image
    with Image.open(path) as im:
        px = im.convert("RGB").getdata()
        total = len(px)
        hits = sum(1 for p in px if p == rgb)
    return hits / total if total else 0.0


def picture_origin(im):
    """Top-left of the 320x200 picture inside the bordered frame."""
    w, h = im.size
    p = im.load()
    border = p[0, 0]
    xs = [x for x in range(w) if any(p[x, y] != border for y in range(0, h, 4))]
    ys = [y for y in range(h) if any(p[x, y] != border for x in range(0, w, 4))]
    if not xs or not ys:                # a frame of one flat colour
        return (w - SCREEN_W) // 2, (h - SCREEN_H) // 2
    return xs[0], ys[0]


def frames_match(a_path, b_path, shimmer_rows):
    """True if two frames agree everywhere the shimmer does not live."""
    from PIL import Image
    with Image.open(a_path) as ia, Image.open(b_path) as ib:
        a, b = ia.convert("RGB"), ib.convert("RGB")
        if a.size != b.size:
            return False
        w, h = a.size
        pa, pb = a.load(), b.load()
        # Only the 320x200 picture, and the border excluded as well as the
        # shimmer: the border carries the current player's colour and
        # flares on every hand-over, so it changes for reasons that say
        # nothing about whether the game is advancing.
        #
        # The origin is FOUND, not assumed.  It is not the centre of the
        # frame - VICE leaves 40 rows above the picture and 48 below - and
        # assuming it was cost an hour of the rows being mislabelled by
        # five, which made the shimmer look like it lived somewhere it does
        # not.
        x0, y0 = picture_origin(a)
        skip = set()
        for r in shimmer_rows:
            for dy in range(CHAR_H):
                skip.add(r * CHAR_H + dy)
        for dy in range(SCREEN_H):
            if dy in skip:
                continue
            y = y0 + dy
            for dx in range(SCREEN_W):
                if pa[x0 + dx, y] != pb[x0 + dx, y]:
                    return False
    return True


def run_once(prg, png, cycles):
    if os.path.exists(png):
        os.remove(png)
    env = dict(os.environ)
    brew = env.get("BREW_PREFIX", "/opt/homebrew")
    env["XDG_DATA_DIRS"] = brew + "/share:" + env.get("XDG_DATA_DIRS", "")
    subprocess.run(
        ["xplus4"] + VICE_FLAGS
        + ["-limitcycles", str(cycles), "-exitscreenshot", png,
           "-autostart", prg],
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return os.path.exists(png) and os.path.getsize(png) > 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    minblack = MIN_BLACK
    maxwhite = None
    stillat = None
    shimrows = SHIMMER_ROWS
    for a in sys.argv[1:]:
        if a.startswith("--still-at="):
            stillat = a.split("=", 1)[1]
        if a.startswith("--shimmer-rows="):
            v = a.split("=", 1)[1]
            shimrows = tuple(int(x) for x in v.split(",")) if v else ()
        if a.startswith("--min-black="):
            minblack = float(a.split("=", 1)[1])
        if a.startswith("--max-white="):
            maxwhite = float(a.split("=", 1)[1])

    if len(args) < 2:
        print(__doc__)
        return 2
    prg, png = args[0], args[1]
    cycles = args[2] if len(args) > 2 else "400000000"

    for attempt in range(1, ATTEMPTS + 1):
        if run_once(prg, png, cycles):
            if maxwhite is not None:
                frac = colour_fraction(png, (255, 255, 255))
                if frac <= maxwhite:
                    if stillat is not None:
                        # A .png suffix, because VICE picks the image
                        # format from the extension and silently writes
                        # nothing for anything it does not recognise -
                        # which made this check pass by never comparing.
                        other = png[:-4] + "-still.png" if png.endswith(".png") \
                                else png + "-still.png"
                        if not run_once(prg, other, stillat):
                            print("*** %s: the second shot produced nothing, "
                                  "so 'is it still moving' was not answered "
                                  "***" % png, file=sys.stderr)
                            return 1
                        same = frames_match(png, other, shimrows)
                        os.remove(other)
                        if same:
                            print("*** %s: nothing outside the shimmer rows "
                                  "changed between %s and %s cycles - the "
                                  "game is not progressing ***"
                                  % (png, cycles, stillat), file=sys.stderr)
                            return 1
                    print("%s: ok (%.0f%% white, attempt %d)"
                          % (png, frac * 100, attempt))
                    return 0
                why = ("%.0f%% white - the BASIC boot screen, so autostart "
                       "lost the RUN" % (frac * 100))
            else:
                frac = colour_fraction(png, (0, 0, 0))
                if frac >= minblack:
                    print("%s: ok (%.0f%% black, attempt %d)"
                          % (png, frac * 100, attempt))
                    return 0
                why = "only %.0f%% black - autostart probably lost the RUN" % (
                    frac * 100)
        else:
            why = "no screenshot produced"
        print("  retry %d/%d: %s" % (attempt, ATTEMPTS, why), file=sys.stderr)

    print("*** %s never ran to completion in %d attempts ***"
          % (prg, ATTEMPTS), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
