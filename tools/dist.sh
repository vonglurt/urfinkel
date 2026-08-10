#!/bin/sh
#
# Pack the downloadable collection: build/urfinkel.zip and build/urfinkel.tar.gz,
# each holding the program, the disk image, an offline copy of the play page,
# a script to serve it, the licence and a page of notes.
#
# REPRODUCIBLE ON PURPOSE.  These two archives are hashed by
# tools/checksums.sh, and those hashes are committed and then checked by
# pre-push.  An archive that differed on every build would fail that check
# every time and the numbers would mean nothing, so every source of variation
# is pinned:
#
#   * every staged file is given the build date as its mtime, so the archive
#     does not change because the day did;
#   * the members are listed by hand in a fixed order rather than left to
#     whatever order the directory happens to be read in;
#   * tar is told to write uid/gid 0 and empty owner names, so an archive
#     built by one person is byte-identical to one built by another;
#   * gzip is called with -n, which leaves the timestamp and the original
#     filename out of its header - the two things that otherwise change every
#     single run.
#
# WHY THE OFFLINE PAGE IS NOT SIMPLY COPIED.  docs/index.html carries a
# generated block of download buttons and checksums.  If that block came along
# unchanged, the archive would contain its own hash: the block lists the hash
# of urfinkel.zip, so writing it would change the zip, which would change the
# hash, which would change the block.  There is no fixed point to settle on.
#
# So the block is replaced with a few lines saying the files are already in
# this folder - which is also just better, since an offline copy has no use
# for buttons that download what is sitting next to it.  The archived page
# then holds no hash at all, and the loop is gone rather than papered over.
#
# The other change is the game's address: the hosted page pulls the .prg from
# raw.githubusercontent.com, and the archived one loads the copy beside it.

set -e

root=$(git rev-parse --show-toplevel)
cd "$root"

BUILD=build
DIST=$BUILD/dist
STAGE=$DIST/urfinkel
ZIP=$BUILD/urfinkel.zip
TGZ=$BUILD/urfinkel.tar.gz
PAGE=docs/index.html

BEGIN='<!-- CHECKSUMS:START -->'
END='<!-- CHECKSUMS:END -->'

for f in $BUILD/urfinkel.prg $BUILD/urfinkel.d64 LICENSE "$PAGE" \
         tools/RUNNING.txt.in tools/start-html.sh; do
    [ -f "$f" ] || { echo "dist: $f is missing - run 'make && make disk' first" >&2; exit 1; }
done

stamp=$(python3 -c "
import re,sys
d=open('$BUILD/urfinkel.prg','rb').read()
m=re.search(rb'20\d\d-\d\d-\d\d',d)
sys.stdout.write(m.group().decode() if m else '')
")
[ -n "$stamp" ] || { echo "dist: no build stamp in $BUILD/urfinkel.prg" >&2; exit 1; }

# ------------------------------------------------------------------- stage
rm -rf "$DIST"
mkdir -p "$STAGE"

cp "$BUILD/urfinkel.prg" "$BUILD/urfinkel.d64" "$STAGE/"
cp LICENSE "$STAGE/LICENCE"
cp tools/start-html.sh "$STAGE/start-html.sh"
chmod +x "$STAGE/start-html.sh"
# The build commands come from `make buildinfo` rather than being written out
# again here, so the notes cannot drift from the recipe.  BUILD_DATE is passed
# so the flags quoted are the ones that produced THIS binary; without it the
# notes would print today's date into a command that rebuilds a different file.
info=$(make buildinfo BUILD_DATE="$stamp" 2>/dev/null) || {
    echo "dist: 'make buildinfo' failed - is the toolchain installed?" >&2; exit 1; }
field() { printf '%s\n' "$info" | awk -F'\t' -v k="$1" '$1==k{print $2; exit}'; }

# awk rather than sed for the substitution: the compile line contains slashes
# and quotes, and picking a sed delimiter it cannot contain is a losing game.
# THE PATHS ARE REWRITTEN FOR THE ARCHIVE'S LAYOUT.  make builds into build/,
# which does not exist once this is unpacked, so a command quoting it would
# fail on its output file the moment anyone tried it.  The rebuilt artefacts
# are also given -rebuilt names rather than the shipped ones, so running the
# command leaves you with something to compare against instead of quietly
# overwriting the file you were checking.
COMPILE="    $(field compile | sed 's|-o build/urfinkel\.prg|-o urfinkel-rebuilt.prg|')" \
DISKIMAGE="    $(field diskimage | sed -e 's|d64 build/urfinkel\.d64|d64 urfinkel-rebuilt.d64|' \
                                       -e 's|-write build/urfinkel\.prg|-write urfinkel-rebuilt.prg|')" \
STAMP="$stamp" \
python3 -c '
import os, sys
t = open("tools/RUNNING.txt.in", encoding="utf-8").read()
for k in ("DATE", "COMPILE", "DISKIMAGE"):
    v = os.environ["STAMP" if k == "DATE" else k]
    if "@%s@" % k not in t:
        sys.exit("dist: RUNNING.txt.in has no @%s@ placeholder" % k)
    t = t.replace("@%s@" % k, v)
open(sys.argv[1], "w", encoding="utf-8").write(t)
' "$STAGE/RUNNING.txt"

# ------------------------------------------------------ the emulator itself
# Vendored rather than fetched at build time: the archives are byte-checked by
# pre-push, and an archive whose contents arrive over the network is not
# reproducible.  It is also somebody else's GPL code - see vendor/emulatorjs/
# SOURCE.txt, which travels with it and is why this is a copy and not a link.
[ -f vendor/emulatorjs/loader.js ] || {
    echo "dist: vendor/emulatorjs is missing - run tools/vendor-emulator.sh" >&2
    exit 1; }
tools/vendor-emulator.sh --check >/dev/null || {
    echo "dist: vendor/emulatorjs does not match its manifest - re-run" >&2
    echo "      tools/vendor-emulator.sh" >&2
    exit 1; }
mkdir -p "$STAGE/emulatorjs"
cp -R vendor/emulatorjs/. "$STAGE/emulatorjs/"

# --------------------------------------------------- the two offline pages
# TWO PAGES, because they answer different questions.  index.html is this
# site's page with its addresses made local - it still pulls the emulator from
# the CDN, which is the smaller download and always the current version.
# index-offline.html additionally points at the emulatorjs/ folder beside it
# and needs nothing from the network at all.
python3 - "$PAGE" "$STAGE/index.html" "$BEGIN" "$END" "$STAGE/index-offline.html" <<'PY'
import re, sys

src, out, begin, end, out_offline = sys.argv[1:6]
html = open(src, encoding="utf-8").read()

OFFLINE = """<p class="dl"><strong>The files are in this folder already</strong> —
this is the offline copy, so there is nothing here to download.</p>
<ul class="dl">
  <li><b>urfinkel.prg</b> — the program alone. Drag it onto VICE, or let the
      page above load it.</li>
  <li><b>urfinkel.d64</b> — the same program as a disk image, for a floppy
      emulator or an SD2IEC.</li>
</ul>
<p class="verify">Sizes, checksums and the exact command that built these are
in <a href="https://github.com/vonglurt/urfinkel/blob/main/build/CHECKSUMS.txt">CHECKSUMS.txt</a>
on GitHub. RUNNING.txt beside this file says how to run either one.</p>"""

# 1. Replace the generated download block. Keeping it would put the archive's
#    own checksum inside the archive - see the note at the top of dist.sh.
if begin not in html or end not in html:
    sys.exit("dist: %s has no CHECKSUMS markers" % src)
head, _, rest = html.partition(begin)
_, _, tail = rest.partition(end)
html = head + OFFLINE + tail

# 2. Point the emulator at the .prg sitting beside this page rather than at
#    raw.githubusercontent.com.
#    The semicolon is matched and rewritten too. Replacing only up to the
#    closing quote left the original ';' stranded after the comment, which
#    survived on automatic semicolon insertion alone - working by luck.
html, n = re.subn(
    r'(EJS_gameUrl\s*=\s*)"[^"]*";',
    r'\1"urfinkel.prg";        // the copy sitting next to this page',
    html)
if n != 1:
    sys.exit("dist: expected exactly one EJS_gameUrl, found %d" % n)

# 3. Say plainly that the emulator itself still comes from a CDN. An offline
#    bundle that quietly needs the network is worse than one that says so.
#
#    Anchored on the control bar, so it lands under the screen AND under the
#    click-me line rather than between them. That line opens with an arrow
#    pointing up at the screen, and anything inserted above it becomes what
#    the arrow appears to mean. It used to be anchored on the hosted page's
#    own .note block, which has since been deleted; the bar is structural and
#    cannot be reworded away without this build saying so.
ANCHOR = '<div class="bar">'

def with_note(page, body):
    out, n = re.subn(re.escape(ANCHOR),
                     '<div class="note">\n' + body + '</div>\n\n' + ANCHOR,
                     page, count=1)
    if n != 1:
        sys.exit("dist: could not find %s to insert the note before" % ANCHOR)
    return out

CDN_NOTE = (
    '  <strong>This is the local copy, using the online emulator.</strong> It\n'
    '  loads <code>urfinkel.prg</code> from the folder it is in, so it must be\n'
    '  served rather than opened directly — run <code>./start-html.sh</code>\n'
    '  next to it. EmulatorJS itself is fetched from its CDN, so this page\n'
    '  wants a connection even though the game does not.\n'
    '  <br><br>\n'
    '  <strong>No connection? Open <code>index-offline.html</code> instead</strong>\n'
    '  — the same page running the emulator from the <code>emulatorjs/</code>\n'
    '  folder beside it, with nothing fetched from anywhere.\n')

OFFLINE_NOTE = (
    '  <strong>This is the fully offline copy.</strong> The game, the\n'
    '  emulator and this page all come from this folder — nothing is fetched\n'
    '  from any network. It still has to be <em>served</em> rather than opened\n'
    '  directly, because a <code>file://</code> page may not read the files\n'
    '  beside it: run <code>./start-html.sh</code>.\n'
    '  <br><br>\n'
    '  The emulator in <code>emulatorjs/</code> is EmulatorJS, which is GPL-3.0\n'
    '  and is not part of UR FINKEL — see <code>emulatorjs/SOURCE.txt</code>.\n'
    '  <code>index.html</code> beside this file is the same page taking the\n'
    '  emulator from its CDN instead, which is always the current version.\n')

open(out, "w", encoding="utf-8").write(with_note(html, CDN_NOTE))

# 4. The offline page differs by two addresses: where the loader comes from,
#    and where the loader is told to look for everything else.  Both are
#    rewritten from the same source rather than kept as a second copy of the
#    page, so the two cannot drift apart.
off, n = re.subn(r'(EJS_pathtodata\s*=\s*)"[^"]*";',
                 r'\1"emulatorjs/";     // the folder beside this page', html)
if n != 1:
    sys.exit("dist: expected exactly one EJS_pathtodata, found %d" % n)

off, n = re.subn(r'(s\.src\s*=\s*)"https://cdn\.emulatorjs\.org/[^"]*";',
                 r'\1"emulatorjs/loader.js";', off)
if n != 1:
    sys.exit("dist: expected exactly one CDN loader URL, found %d" % n)

if "cdn.emulatorjs.org" in off:
    sys.exit("dist: the offline page still refers to the CDN somewhere")

open(out_offline, "w", encoding="utf-8").write(with_note(off, OFFLINE_NOTE))
PY

# ----------------------------------------------------------------- the source
# THE .s FILES COME TOO, though only .c and .h were asked for.  blit.s and
# irq.s are linked into the program - the blitter and the raster interrupt -
# so a src/ without them is a source tree that cannot be built, which is a
# worse thing to ship than none at all.
mkdir -p "$STAGE/src"
cp src/*.c src/*.h src/*.s "$STAGE/src/"

# -------------------------------------------------------------- the index
# A map of the archive: what every file in it is, and what build it belongs
# to.  The descriptions are not written here - each source file opens with a
# "name - what it is" line and that line is what gets quoted, so this cannot
# describe a file as something it stopped being.
#
# HOW A BUILD IS IDENTIFIED, AND WHY NOT BY COMMIT.  A commit SHA would be
# the obvious unique id and it cannot be used, for a reason worth writing
# down: this file goes inside the archives, and the archives' hashes are
# published and then checked by pre-push.  pre-commit builds them while HEAD
# is still the PARENT commit; pre-push rebuilds them once HEAD is the new
# commit.  Writing HEAD in would make those two archives differ and every
# push would be refused by a check that was working correctly.
#
# What identifies a build instead is intrinsic to it: the date compiled into
# the program, git's blob SHA for each binary - a hash of the content, which
# two builds on the same day do not share - and a fingerprint over the blob
# SHAs of every source file.  All three are computable before any commit
# exists, and anyone can reproduce them with git hash-object.
SRCFILES=$(cd "$STAGE" && ls src/*.c src/*.h src/*.s | LC_ALL=C sort)

STAMP="$stamp" SRCLIST="$SRCFILES" python3 - "$STAGE" <<'PY'
import hashlib, os, subprocess, sys

stage = sys.argv[1]
stamp = os.environ["STAMP"]
srcs  = os.environ["SRCLIST"].split()

def blob(path):
    return subprocess.run(["git", "hash-object", os.path.join(stage, path)],
                          capture_output=True, text=True, check=True).stdout.strip()

def described(path):
    """The 'name - what it is' line every hand-written source file opens with.

    The generated headers do not follow that convention - they open by saying
    which tool wrote them - so that first line is quoted instead. Falling
    through to an empty description would have left three blank entries in a
    file whose whole job is saying what everything is.
    """
    head = []
    with open(os.path.join(stage, path), encoding="utf-8", errors="replace") as fh:
        head = list(fh)[:6]
    for line in head:
        s = line.strip().lstrip("*;/ ").strip()
        if s.startswith(os.path.basename(path) + " - "):
            return s.split(" - ", 1)[1].rstrip()
    for line in head:
        s = line.strip().lstrip("*;/ ").strip()
        if s.startswith("GENERATED by"):
            return s.rstrip(" .") + " (rebuild with `make music`)"
    return "(no description in the file)"

src_blobs = [(p, blob(p)) for p in srcs]
fingerprint = hashlib.sha256(
    "".join("%s  %s\n" % (h, p) for p, h in src_blobs).encode()).hexdigest()

TOP = {
    "urfinkel.prg":  "the compiled program - this is the game",
    "urfinkel.d64":  "a 1541 disk image holding the same program",
    "index.html":    "the play page, emulator fetched from its CDN",
    "index-offline.html": "the same page, emulator from emulatorjs/ - needs no network",
    "start-html.sh": "serves this folder on a free port and opens that page",
    "RUNNING.txt":   "how to run it, three ways, and how to rebuild it",
    "index.txt":     "this file",
    "LICENCE":       "MIT - covers UR FINKEL: the binaries and src/",
}

out = []
w = out.append
w("UR FINKEL - what is in this archive")
w("===================================")
w("")
w("The Royal Game of Ur for the Commodore Plus/4, written in C and 6502")
w("assembly.  The project, its history and its documentation are at:")
w("")
w("    https://github.com/vonglurt/urfinkel")
w("")
w("")
w("WHICH BUILD THIS IS")
w("-------------------")
w("")
w("build date          %s" % stamp)
w("                    Compiled into the program and drawn on its menu, so")
w("                    a copy can be identified from the machine itself.")
w("urfinkel.prg        %s" % blob("urfinkel.prg"))
w("urfinkel.d64        %s" % blob("urfinkel.d64"))
w("source fingerprint  %s" % fingerprint)
w("")
w("The two long numbers are git's own address for each file's content -")
w("reproduce either with `git hash-object urfinkel.prg`.  They identify this")
w("build exactly, where the date alone would not: two builds made on the")
w("same day share a date and not a hash.")
w("")
w("The fingerprint is a SHA-256 over the blob SHA of every file in src/,")
w("listed in name order, so it changes if any source file does.")
w("")
w("THERE IS NO COMMIT SHA HERE, deliberately.  This file is inside the")
w("archive, and the archive's own checksum is published and then verified")
w("before a push.  A commit cannot name itself: writing the SHA in would")
w("change the archive it describes.  To find the commit, GitHub's history")
w("for the binary resolves to it and cannot go stale:")
w("")
w("    https://github.com/vonglurt/urfinkel/commits/main/build/urfinkel.prg")
w("")
w("")
w("THE FILES")
w("---------")
w("")
for name in ("urfinkel.prg", "urfinkel.d64", "index.html", "index-offline.html",
             "start-html.sh", "RUNNING.txt", "index.txt", "LICENCE"):
    w("%-20s %s" % (name, TOP[name]))
w("%-20s %s" % ("emulatorjs/", "EmulatorJS - GPL-3.0, NOT part of UR FINKEL"))
w("")
w("The emulator in emulatorjs/ is somebody else's work under a different")
w("licence, included so index-offline.html needs no network. LICENCE above")
w("covers UR FINKEL only; emulatorjs/LICENSE and emulatorjs/SOURCE.txt cover")
w("that folder and say where its source is.")
w("")
w("")
w("src/ - THE SOURCE")
w("-----------------")
w("")
w("Everything the program is built from.  RUNNING.txt carries the exact")
w("cl65 command that turns these back into the urfinkel.prg beside them.")
w("Each line below is the file's own opening description.")
w("")
for p, _ in src_blobs:
    w("%-18s %s" % (os.path.basename(p), described(p)))
w("")

open(os.path.join(stage, "index.txt"), "w", encoding="utf-8").write("\n".join(out) + "\n")
PY

# --------------------------------------------------------------- reproduce
# One mtime for everything, taken from the build rather than from the clock.
# Run after the source and the index are staged, so those files are stamped too.
find "$DIST" -exec touch -t "$(echo "$stamp" | tr -d '-')0000" {} +

# LC_ALL=C so the sort is byte order rather than whatever the locale thinks,
# which is what keeps the member order the same on someone else's machine.
SRCMEMBERS=$(cd "$STAGE" && ls src/*.c src/*.h src/*.s | LC_ALL=C sort | sed 's|^|urfinkel/|' | tr '\n' ' ')

# The vendored emulator, found rather than listed: its file set is decided by
# tools/vendor-emulator.sh and would otherwise have to be kept in step here.
EJSMEMBERS=$(cd "$STAGE" && find emulatorjs -type f | LC_ALL=C sort | sed 's|^|urfinkel/|' | tr '\n' ' ')

MEMBERS="urfinkel/LICENCE urfinkel/RUNNING.txt urfinkel/index.html \
         urfinkel/index.txt urfinkel/index-offline.html urfinkel/start-html.sh \
         urfinkel/urfinkel.d64 urfinkel/urfinkel.prg \
         $SRCMEMBERS $EJSMEMBERS"

rm -f "$ZIP" "$TGZ"

# -X drops the uid/gid and extended attributes that would otherwise make the
# archive differ between machines and between filesystems.
( cd "$DIST" && zip -X -q -9 "../urfinkel.zip" $MEMBERS )

# bsdtar (macOS) and GNU tar (everywhere else) spell the same normalisation
# differently, so each is asked in its own words.
if tar --version 2>&1 | grep -qi bsdtar; then
    ( cd "$DIST" && tar --format ustar --uid 0 --gid 0 --uname "" --gname "" \
        -cf - $MEMBERS ) | gzip -n -9 > "$TGZ"
else
    ( cd "$DIST" && tar --format=ustar --owner=0 --group=0 --numeric-owner \
        --mtime="$stamp 00:00:00Z" -cf - $MEMBERS ) | gzip -n -9 > "$TGZ"
fi

# The staging tree has served its purpose; leaving it would put a second copy
# of both binaries in build/ for no reason.
rm -rf "$DIST"

echo "dist: $ZIP ($(wc -c < "$ZIP" | tr -d ' ') bytes)"
echo "dist: $TGZ ($(wc -c < "$TGZ" | tr -d ' ') bytes)"
