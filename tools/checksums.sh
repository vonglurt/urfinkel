#!/bin/sh
#
# Hash the published binaries, write a sidecar checksum file for each binary
# and each algorithm, and paste a table of the result into README.md between
# its CHECKSUMS markers.
#
# WHY THIS EXISTS.  build/urfinkel.prg and build/urfinkel.d64 are offered as
# downloads, so a visitor needs a way to tell that the file that reached them
# is the file that left here.  Hand-written hashes would be wrong by the second
# commit; hand-written byte counts already were.  This is the one place the
# numbers are produced, and everything that quotes them is filled in from it.
#
# ONE FILE PER BINARY PER ALGORITHM:
#
#     build/urfinkel.prg.sha256      build/urfinkel.prg.md5
#     build/urfinkel.d64.sha256      build/urfinkel.d64.md5
#
# Each holds exactly one line in the format the checking tools already expect -
# the digest, two spaces, the bare filename - and nothing else.  No header, no
# comment, no second algorithm.  That is the whole point of the split: a file
# with only the lines its tool understands is checked without a warning, and
# the name says what it is, so a visitor who has downloaded urfinkel.d64 and
# urfinkel.d64.sha256 into one folder can run
#
#     shasum -a 256 -c urfinkel.d64.sha256
#     md5sum -c urfinkel.d64.md5
#
# and get one line of output that says OK.  The bare filename is deliberate:
# it makes the sidecar work in the folder the download landed in rather than
# only at the root of a clone.
#
# AND ONE MANIFEST, build/CHECKSUMS.txt, which is for a reader rather than a
# checking tool: the hashes again, plus what produced them - the compiler and
# its version, the exact command line, every source file with its git blob
# SHA.  Enough to answer "what is this build, and could I make it again".
#
# WHAT THE MANIFEST DELIBERATELY LEAVES OUT: the host OS, the architecture,
# the absolute path cl65 was found at, and the time of day.  None of them
# changes the bytes that come out, all of them differ between machines, and
# the manifest is committed and then checked by pre-push - so recording them
# would fail that check for a build that is in fact identical.  What is
# recorded is what determines the artefact, and nothing else.
#
# WHAT IT CANNOT DO.  It cannot stamp the README with the SHA of the commit
# that carries it.  That commit's SHA is a hash of its own content, README
# included, so writing the SHA in would change the SHA - a fixed point no hook
# can reach, not a limitation of this script.  What stands in for it:
#
#   * the BUILD DATE, read back out of the .prg where the Makefile stamped it,
#     which is the identity the machine itself draws on the menu;
#   * the GIT BLOB SHA of each binary - git's own content address, computable
#     before any commit exists, and exactly what GitHub stores that file as.
#     Anyone can reproduce it with `git hash-object build/urfinkel.prg`;
#   * a link to that file's commit history, which always resolves to whichever
#     commit last changed it, and so is never stale.
#
# Usage:
#   tools/checksums.sh            regenerate the sidecars, README and play page
#   tools/checksums.sh --check    exit 1 if any is out of date, change nothing

set -e

root=$(git rev-parse --show-toplevel)
cd "$root"

CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

README=README.md
PLAYPAGE=docs/index.html
INSTALL=INSTALL.md
BEGIN='<!-- CHECKSUMS:START -->'
END='<!-- CHECKSUMS:END -->'
PRGBEGIN='<!-- PRGSIZE:START -->'
PRGEND='<!-- PRGSIZE:END -->'
REPO=https://github.com/vonglurt/urfinkel

FILES="build/urfinkel.prg build/urfinkel.d64 build/urfinkel.zip build/urfinkel.tar.gz"
ALGOS="sha256 md5"

# What each artefact is, in the words the play page already used for it.  The
# byte count beside it used to be typed in by hand and had been wrong before.
# The machine is filled in by the caller from `make buildinfo`, so these read
# "the program alone, for the Commodore Plus/4" and would follow the target if
# a second one is ever added rather than quietly describing the wrong machine.
what() {
    case "$1" in
        *.prg)    echo "the program alone, for the $2" ;;
        *.d64)    echo "a disk image, for the $2" ;;
        *.zip)    echo "the whole collection, zipped" ;;
        *.tar.gz) echo "the same collection as a gzipped tar, for Linux and macOS" ;;
    esac
}

# The archives are the offline kit - the program, a disk image and VICE will
# play this on a PC with the network unplugged - and that is the reason to
# take one rather than a loose binary, so it is said on the button instead of
# left to be inferred from a file list.
# One label per line; the caller emits a span for each.
tag() {
    case "$1" in
        *.zip|*.tar.gz)
            echo 'plays offline on a PC'
            echo 'full source included' ;;
        *) ;;
    esac
}

# HOW TO USE EACH ONE, said beside the button it belongs to rather than in a
# paragraph above the list.  A visitor reading about the .d64 is looking at
# the .d64, not counting bullets back to the third one.
use() {
    case "$1" in
        *.prg) echo 'Drag it onto VICE, YAPE or plus4emu. It starts faster than the disk image — there is no directory to read first.' ;;
        *.d64) echo 'Put it on an SD2IEC or a Pi1541, mount it as a disk, then <code>dload"urfinkel"</code>. Desktop emulators open it directly.' ;;
        *.zip|*.tar.gz) echo 'Unpack it anywhere. Holds the program, the disk image, the whole C and 6502 source tree, this page with a script that serves it on your own machine, the licence, and notes — including the exact <code>cl65</code> command that rebuilds the binary byte-for-byte from the source beside it. The emulator comes too, so the browser copy plays with nothing plugged in.' ;;
    esac
}

# The small print, on the archives only.  It is the offline browser copy for a
# PC or a Mac, and everything a person needs to know before taking it - that it
# must be served rather than double-clicked, and that the emulator inside it is
# somebody else's GPL code - belongs with the button rather than in a banner
# they meet only after unpacking.
fine() {
    case "$1" in
        *.zip|*.tar.gz)
            echo 'Fully offline: the game, the emulator and the page all come out of the folder — nothing is fetched from anywhere. It has to be <b>served rather than opened directly</b>, because a <code>file://</code> page may not read the files beside it: run <code>./start-html.sh</code>. The emulator in <code>emulatorjs/</code> is EmulatorJS, GPL-3.0 and not part of UR FINKEL — see <code>emulatorjs/SOURCE.txt</code>. <code>index.html</code> beside it is the same page taking the emulator from its CDN instead, which is always the current version.' ;;
        *) ;;
    esac
}

# The class the stylesheet hangs a gradient on.  Taken from the whole suffix
# rather than the last dot, because "tar.gz" would otherwise arrive as "gz".
kind() {
    case "$1" in
        *.tar.gz) echo targz ;;
        *)        echo "${1##*.}" ;;
    esac
}

# A glyph for each kind, so the two downloads are told apart before the
# extension is read: a page of code for the loose program, a floppy for the
# disk image.  Drawn inline as strokes in currentColor rather than fetched,
# because this page must keep working with nothing but itself, and so the
# icon takes the link's colour on hover without a second rule.
#
# The `download` attribute on the link is inert - browsers ignore it across
# origins, and these point at github.com.  What actually saves the file rather
# than displaying it is GitHub serving both binaries as
# application/octet-stream; the .sha256 and .md5 come back as text/plain and
# open in the browser, which is why the page tells you to save those yourself.
icon() {
    case "$1" in
        *.prg) echo '<svg class="ico" viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path d="M3.5 1.5h6l3 3v10h-9z"/><path d="M9.5 1.5v3h3"/><path d="M5.5 8.5 7 10l-1.5 1.5M8.5 11.5h2"/></svg>' ;;
        *.zip|*.tar.gz) echo '<svg class="ico" viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path d="M1.5 4.8 8 1.7l6.5 3.1v6.4L8 14.3l-6.5-3.1z"/><path d="M1.5 4.8 8 8l6.5-3.2M8 8v6.3"/></svg>' ;;
        *.d64) echo '<svg class="ico" viewBox="0 0 16 16" aria-hidden="true" focusable="false"><path d="M1.5 1.5h10l3 3v10h-13z"/><path d="M4.5 1.5h6v4h-6zM4.5 9.5h7v5h-7z"/></svg>' ;;
    esac
}

for f in $FILES; do
    if [ ! -f "$f" ]; then
        echo "checksums: $f is missing - run 'make && make disk' first" >&2
        exit 1
    fi
done

# macOS ships `md5` and `shasum`; GNU userlands ship `md5sum` and `sha256sum`.
# Only the bare digest is taken from either, because the line around it is
# assembled here - the two differ on how they print it, and the sidecar has to
# be identical whichever machine wrote it.
digest() {  # digest <algo> <path>
    case "$1" in
        md5)
            if command -v md5 >/dev/null 2>&1; then md5 -q "$2"
            else md5sum "$2" | cut -d' ' -f1
            fi ;;
        sha256)
            if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$2" | cut -d' ' -f1
            else sha256sum "$2" | cut -d' ' -f1
            fi ;;
    esac
}

size_of() { wc -c < "$1" | tr -d ' '; }

# 56511 -> "56 511", matching how every other byte count in the prose is set.
group() { echo "$1" | sed -e :a -e 's/\(.*[0-9]\)\([0-9]\{3\}\)/\1 \2/;ta'; }

# The Makefile compiles `date +%Y-%m-%d` into the program and front.c draws it
# on the cabinet, so the binary carries its own build date as plain ASCII.
# Reading it back out is how pre-push identifies a build; the same trick names
# the build here rather than trusting today's clock, which would be a lie for
# any file that was not rebuilt just now.
stamp=$(python3 -c "
import re,sys
d=open('build/urfinkel.prg','rb').read()
m=re.search(rb'20\d\d-\d\d-\d\d',d)
sys.stdout.write(m.group().decode() if m else '')
")
[ -n "$stamp" ] || { echo "checksums: no build stamp in build/urfinkel.prg" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# ------------------------------------------------------------- the sidecars
for f in $FILES; do
    b=$(basename "$f")
    for a in $ALGOS; do
        printf '%s  %s\n' "$(digest "$a" "$f")" "$b" > "$tmp/$b.$a"
    done
done

# -------------------------------------------------------------- the manifest
# BUILD_DATE is passed so the flags recorded are the ones that produced THIS
# binary rather than the ones today's clock would produce.  Without it, a
# manifest regenerated tomorrow for a binary built today would differ in the
# -DBUILD_DATE flag, and pre-push would refuse a push that is perfectly sound.
info=$(make buildinfo BUILD_DATE="$stamp" 2>/dev/null) || {
    echo "checksums: 'make buildinfo' failed - is the toolchain installed?" >&2
    exit 1
}
field() { printf '%s\n' "$info" | awk -F'\t' -v k="$1" '$1==k{print $2; exit}'; }

# "plus4 (Commodore Plus/4)" -> "Commodore Plus/4".  The machine is named on
# every download rather than left implicit, because a file called urfinkel.prg
# says nothing about what it runs on to anyone who has not met a Plus/4 - and
# because a second target would otherwise inherit the first one's description
# silently.  Taken from `make buildinfo` so that renaming the target in the
# Makefile renames it everywhere it is written down.
machine=$(field target | sed -e 's/^[^(]*(//' -e 's/)$//')
[ -n "$machine" ] || machine=$(field target)

{
    echo "UR FINKEL - build manifest"
    echo "=========================="
    echo
    echo "What this build is, what produced it, and how to tell that a copy of"
    echo "it arrived intact. Generated by tools/checksums.sh; do not edit."
    echo
    echo "BUILD"
    echo "-----"
    echo "date          $stamp"
    echo "              Compiled into the program and drawn on its menu, so a"
    echo "              copy can be identified from the machine itself."
    echo "target        $(field target)"
    echo
    echo "TOOLCHAIN"
    echo "---------"
    echo "compiler      $(field cl65)"
    echo "disk image    $(field c1541)"
    echo
    echo "The host OS, the architecture and the path the tools were found at are"
    echo "deliberately not recorded: none of them changes the bytes that come"
    echo "out, and all of them differ between machines."
    echo
    echo "COMMANDS"
    echo "--------"
    echo "Run from the root of a clone, these two reproduce the artefacts below"
    echo "exactly. Both were run verbatim and compared byte for byte."
    echo
    echo "    $(field compile)"
    echo
    echo "    $(field diskimage)"
    echo
    echo "SOURCES"
    echo "-------"
    echo "Every file the command above compiles, and every header they include,"
    echo "with git's own address for its content. Reproduce any line with"
    echo "\`git hash-object <path>\`."
    echo
    for s in $(field sources) $(field headers); do
        printf '%-20s %s\n' "$s" "$(git hash-object "$s")"
    done
    echo
    echo "ARTEFACTS"
    echo "---------"
    for f in $FILES; do
        b=$(basename "$f")
        echo "$b"
        printf '  %-12s %s bytes\n' "size" "$(group "$(size_of "$f")")"
        printf '  %-12s %s\n' "md5" "$(digest md5 "$f")"
        printf '  %-12s %s\n' "sha256" "$(digest sha256 "$f")"
        printf '  %-12s %s\n' "git blob" "$(git hash-object "$f")"
        echo
    done
    echo "VERIFYING A DOWNLOAD"
    echo "--------------------"
    echo "Each hash above is also published on its own, as the single line the"
    echo "checking tool expects. Save one next to the file you downloaded:"
    echo
    for f in $FILES; do
        b=$(basename "$f")
        echo "    shasum -a 256 -c $b.sha256"
        echo "    md5sum -c $b.md5"
    done
    echo
    echo "Do not point those tools at this file - it is prose, and they would"
    echo "have nothing to read in it."
} > "$tmp/CHECKSUMS.txt"

# ---------------------------------------------------------------- the table
{
    echo "$BEGIN"
    echo "<!-- Generated by tools/checksums.sh - do not edit between these markers. -->"
    echo
    echo "| File | Bytes | MD5 | SHA-256 |"
    echo "|---|---:|---|---|"
    for f in $FILES; do
        b=$(basename "$f")
        printf '| [%s](%s/raw/main/%s) | %s | `%s` | `%s` |\n' \
            "$b" "$REPO" "$f" "$(group "$(size_of "$f")")" \
            "$(digest md5 "$f")" "$(digest sha256 "$f")"
    done
    echo
    printf 'Built **%s** — the date the program stamps on its own menu, so a\n' "$stamp"
    echo "download can be identified from the machine without unpacking it."
    echo
    echo "### Verifying a download"
    echo
    echo "Every hash above is also published as a sidecar file holding the single"
    echo "line its checking tool expects. Save one next to the file you downloaded,"
    echo "then run the command beside it:"
    echo
    echo "| Sidecar | Verify with |"
    echo "|---|---|"
    for f in $FILES; do
        b=$(basename "$f")
        printf '| [%s.sha256](%s/raw/main/%s.sha256) | `shasum -a 256 -c %s.sha256` |\n' "$b" "$REPO" "$f" "$b"
        printf '| [%s.md5](%s/raw/main/%s.md5) | `md5sum -c %s.md5` |\n' "$b" "$REPO" "$f" "$b"
    done
    echo
    echo "Each prints one line ending \`OK\`. The filename inside is bare, so this"
    echo "works in whatever folder the download landed in."
    echo
    echo "### What produced these"
    echo
    printf 'The build manifest — **[build/CHECKSUMS.txt](%s/blob/main/build/CHECKSUMS.txt)** —\n' "$REPO"
    echo "records the compiler and its version, the exact command line that was"
    echo "run, and every source file with its git blob SHA. It is written for a"
    echo "reader rather than a checking tool; the sidecars above are the ones to"
    echo "point \`shasum\` and \`md5sum\` at."
    echo
    echo "### Which commit built these"
    echo
    echo "| File | Git blob SHA-1 | Last changed by |"
    echo "|---|---|---|"
    for f in $FILES; do
        b=$(basename "$f")
        printf '| %s | `%s` | [commits touching this file](%s/commits/main/%s) |\n' \
            "$b" "$(git hash-object "$f")" "$REPO" "$f"
    done
    echo
    echo "The blob SHA is git's own address for that content — reproduce it with"
    echo "\`git hash-object build/urfinkel.prg\`, and it is the object GitHub serves"
    echo "the file from. It is used here in place of a commit SHA because a commit"
    echo "cannot state its own: the SHA covers this README, so writing it in would"
    echo "change it. The history link resolves to the right commit instead, and"
    echo "cannot go stale."
    echo
    echo "$END"
} > "$tmp/table.md"

# ------------------------------------------------- the play page's downloads
# The same facts as the README table, shaped for a visitor who came to click
# rather than to read: the filename is the link and carries the weight, what
# it is and how big sits under it, and the digests go below that - indented,
# smaller, and quiet, because they are reference material you only want once
# the file is already on your disk.  The styling lives in the page's own
# <style> block, which is outside these markers and hand-written.
{
    echo "$BEGIN"
    echo "<!-- Generated by tools/checksums.sh - do not edit between these markers. -->"
    echo '<ul class="dl">'
    for f in $FILES; do
        b=$(basename "$f")
        # An id per entry, so the cards above can jump straight to the one
        # they name rather than at the block as a whole.
        printf '  <li id="dl-%s">\n' "$(kind "$b")"
        # The kind is emitted as a class so the stylesheet can give the two
        # buttons gradients running opposite ways without counting children.
        printf '    <a class="file %s" href="%s/raw/main/%s" download>%s<span>%s</span></a>\n' \
            "$(kind "$b")" "$REPO" "$f" "$(icon "$b")" "$b"
        tag "$b" | while IFS= read -r t; do
            [ -z "$t" ] || printf '    <span class="tag">%s</span>\n' "$t"
        done
        printf '    <span class="what">%s — %s bytes — built <b>%s</b></span>\n' \
            "$(what "$b" "$machine")" "$(group "$(size_of "$f")")" "$stamp"
        # The address the button goes to, spelled out.  A button hides where it
        # leads, and this is a file someone may want to fetch with curl or wget,
        # or simply satisfy themselves about before clicking.
        printf '    <span class="use">%s</span>\n' "$(use "$b")"
        printf '    <a class="path" href="%s/raw/main/%s">%s/raw/main/%s</a>\n' \
            "$REPO" "$f" "$REPO" "$f"
        fp=$(fine "$b")
        [ -z "$fp" ] || printf '    <span class="fine">%s</span>\n' "$fp"
        echo '    <dl class="sums">'
        printf '      <dt>SHA-256</dt><dd><code>%s</code> — <a href="%s/raw/main/%s.sha256">%s.sha256</a></dd>\n' \
            "$(digest sha256 "$f")" "$REPO" "$f" "$b"
        printf '      <dt>MD5</dt><dd><code>%s</code> — <a href="%s/raw/main/%s.md5">%s.md5</a></dd>\n' \
            "$(digest md5 "$f")" "$REPO" "$f" "$b"
        echo '    </dl>'
        echo '  </li>'
    done
    echo '</ul>'
    echo '<p class="verify">To check a download arrived intact, save'
    echo 'its <code>.sha256</code> beside it and run <code>shasum -a 256 -c'
    printf 'urfinkel.prg.sha256</code>. What built these — compiler, version and the\n'
    printf 'exact command — is in <a href="%s/blob/main/build/CHECKSUMS.txt">CHECKSUMS.txt</a>.</p>\n' "$REPO"
    echo "$END"
} > "$tmp/play.html"

# ------------------------------------------------- INSTALL.md's two bullets
# The same two files again, in the shape INSTALL.md already used for them.
# These byte counts were typed in by hand and had been wrong; they are the
# last of the three places that quoted a size without deriving it.
{
    echo "$BEGIN"
    echo "<!-- Generated by tools/checksums.sh - do not edit between these markers. -->"
    for f in build/urfinkel.d64 build/urfinkel.prg; do
        b=$(basename "$f")
        printf -- '- [**%s**](%s/raw/main/%s) — %s (%s B)\n' \
            "$b" "$REPO" "$f" "$(what "$b" "$machine" | sed 's/, for the .*//')" \
            "$(group "$(size_of "$f")")"
    done
    echo "$END"
} > "$tmp/install-block.md"
# NOT "install.md": macOS is case-insensitive, so that name and the spliced
# output "INSTALL.md" are one file. The shell truncates the output before
# python opens the block, so the block read back empty and the splice wrote
# nothing - taking INSTALL.md's markers with it. Distinct names, not luck.

# ------------------------------------------------ the README's prose figure
# One number inside a sentence, so it gets a marker of its own rather than
# being swept up in a block: the words around it are hand-written and stay so.
printf '%s**%s bytes**%s' "$PRGBEGIN" "$(group "$(size_of build/urfinkel.prg)")" "$PRGEND" \
    > "$tmp/prgsize.md"

# ------------------------------------------------------------- splice it in
# One splice, used for both files: everything between the markers is replaced
# and everything outside them is left alone, so the prose around each block
# stays hand-written.
splice() {  # splice <file> <block> <out> [begin] [end]
    _b=${4:-$BEGIN}; _e=${5:-$END}
    if ! grep -qF "$_b" "$1" || ! grep -qF "$_e" "$1"; then
        echo "checksums: $1 has no $_b / $_e markers" >&2
        exit 1
    fi
    python3 - "$1" "$2" "$_b" "$_e" > "$3" <<'PY'
import sys
target, block_file, begin, end = sys.argv[1:5]
text  = open(target,     encoding="utf-8").read()
block = open(block_file, encoding="utf-8").read().rstrip("\n")
head, _, rest = text.partition(begin)
_, _, tail    = rest.partition(end)
sys.stdout.write(head + block + tail)
PY
}

splice "$README"   "$tmp/table.md"   "$tmp/README.md"
splice "$PLAYPAGE" "$tmp/play.html"  "$tmp/index.html"
splice "$INSTALL"  "$tmp/install-block.md" "$tmp/INSTALL.md"
# The prose figure is spliced into the README that was just written, not into
# the one on disk, so both edits land in one file rather than one overwriting
# the other.
mv "$tmp/README.md" "$tmp/README.stage"
splice "$tmp/README.stage" "$tmp/prgsize.md" "$tmp/README.md" "$PRGBEGIN" "$PRGEND"

# ------------------------------------------------------------------ deliver
stale=0
for f in $FILES; do
    b=$(basename "$f")
    for a in $ALGOS; do
        cmp -s "$tmp/$b.$a" "build/$b.$a" || stale=1
    done
done
cmp -s "$tmp/CHECKSUMS.txt" build/CHECKSUMS.txt || stale=1
cmp -s "$tmp/README.md"    "$README"           || stale=1
cmp -s "$tmp/index.html"   "$PLAYPAGE"         || stale=1
cmp -s "$tmp/INSTALL.md"   "$INSTALL"          || stale=1

if [ "$CHECK" = "1" ]; then
    if [ "$stale" = "1" ]; then
        echo "checksums: the manifest, the sidecars, $README, $PLAYPAGE or" >&2
        echo "           $INSTALL do" >&2
        echo "           not match the binaries." >&2
        echo "           Run 'make checksums' and commit the result." >&2
        exit 1
    fi
    echo "checksums: up to date"
    exit 0
fi

for f in $FILES; do
    b=$(basename "$f")
    for a in $ALGOS; do cp "$tmp/$b.$a" "build/$b.$a"; done
done
cp "$tmp/CHECKSUMS.txt" build/CHECKSUMS.txt
cp "$tmp/README.md"     "$README"
cp "$tmp/index.html"    "$PLAYPAGE"
cp "$tmp/INSTALL.md"    "$INSTALL"

if [ "$stale" = "1" ]; then
    echo "checksums: manifest, sidecars, $README, $PLAYPAGE and $INSTALL updated ($stamp)"
else
    echo "checksums: unchanged ($stamp)"
fi

exit 0
