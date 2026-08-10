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
sed "s/@DATE@/$stamp/g" tools/RUNNING.txt.in > "$STAGE/RUNNING.txt"

# --------------------------------------------------- the offline play page
python3 - "$PAGE" "$STAGE/index.html" "$BEGIN" "$END" <<'PY'
import re, sys

src, out, begin, end = sys.argv[1:5]
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
note = ('<div class="note">\n'
        '  <strong>This is the offline copy.</strong> It loads\n'
        '  <code>urfinkel.prg</code> from the folder it is in, so it must be\n'
        '  served rather than opened directly — run <code>./start-html.sh</code>\n'
        '  next to it. EmulatorJS, the emulator itself, is still fetched from\n'
        '  its CDN, so this page needs a connection even though the game does\n'
        '  not. With no connection, use VICE and the <code>.prg</code>.\n'
        '</div>\n\n' + ANCHOR)
html, n = re.subn(re.escape(ANCHOR), note, html, count=1)
if n != 1:
    sys.exit("dist: could not find %s to insert the offline note before" % ANCHOR)

open(out, "w", encoding="utf-8").write(html)
PY

# --------------------------------------------------------------- reproduce
# One mtime for everything, taken from the build rather than from the clock.
find "$DIST" -exec touch -t "$(echo "$stamp" | tr -d '-')0000" {} +

MEMBERS="urfinkel/LICENCE urfinkel/RUNNING.txt urfinkel/index.html \
         urfinkel/start-html.sh urfinkel/urfinkel.d64 urfinkel/urfinkel.prg"

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
