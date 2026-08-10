#!/bin/sh
#
# Fetch EmulatorJS into vendor/emulatorjs/ so the offline copy of the play
# page can run without a network.
#
# THIS IS SOMEONE ELSE'S CODE, AND IT IS GPL.  EmulatorJS is GPL-3.0 and the
# core it loads here is VICE, which is GPL-2.0-or-later.  UR FINKEL is MIT.
# Putting them in one archive is mere aggregation - each keeps its own licence
# and neither infects the other - but redistributing the GPL half brings
# obligations, so the fetch also brings down the licence text and writes a
# SOURCE.txt saying exactly which version this is and where its source lives.
# Do not delete either of those from vendor/.
#
# PINNED, NOT "STABLE".  The CDN serves /stable/, which moves.  A moving
# dependency inside a byte-reproducible archive is a contradiction: the same
# sources would produce different archives on different days and pre-push
# would refuse the lot.  So the version is written down here and the files
# come from the versioned path.
#
# WHY THESE FILES.  loader.js pulls emulator.min.js and emulator.min.css; the
# emulator then fetches its core.  The core is a 7z archive - its first bytes
# are 37 7a bc af - so compression/extract7z.js is not optional, it is how the
# core is unpacked at all.  The zip and rar extractors are along for the ride
# because the emulator picks a decompressor from the file it is handed, and a
# missing one is a blank screen rather than an error anyone can read.
#
# Usage:  tools/vendor-emulator.sh          fetch (or re-fetch) into vendor/
#         tools/vendor-emulator.sh --check  verify what is there, change nothing

set -e

VERSION=4.2.3
BASE=https://cdn.emulatorjs.org/$VERSION/data
LICENSE_URL=https://raw.githubusercontent.com/EmulatorJS/EmulatorJS/v$VERSION/LICENSE

# The Plus/4 core. EJS_core = "plus4" resolves inside emulator.min.js to
# "vice_xplus4" (the table there reads plus4:["vice_xplus4"]), and that name
# is what the core file is called.
CORE=vice_xplus4

# ALL THREE CORE BUILDS, because the browser picks one at runtime and the
# choice is not ours.  A page served without the cross-origin isolation
# headers has no SharedArrayBuffer, so EmulatorJS takes the -legacy build;
# with them it may take -thread.  Vendoring only the plain one is what made
# the first offline bundle quietly fetch 1.4 MB from the CDN instead: the
# local file 404s, and EmulatorJS falls back to its own CDN rather than
# failing, so the page still worked and the bundle was not offline at all.
FILES="loader.js
emulator.min.js
emulator.min.css
version.json
localization/en-US.json
cores/$CORE-wasm.data
cores/$CORE-legacy-wasm.data
cores/$CORE-thread-wasm.data
cores/reports/$CORE.json
compression/extract7z.js
compression/extractzip.js
compression/libunrar.js
compression/libunrar.wasm"

root=$(git rev-parse --show-toplevel)
cd "$root"
DEST=vendor/emulatorjs

CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

sha() {
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
    else sha256sum "$1" | cut -d' ' -f1
    fi
}

if [ "$CHECK" = "1" ]; then
    [ -f "$DEST/MANIFEST.sha256" ] || {
        echo "vendor: $DEST is not populated - run tools/vendor-emulator.sh" >&2; exit 1; }
    fail=0
    while read -r want path; do
        [ -n "$path" ] || continue
        [ -f "$DEST/$path" ] || { echo "vendor: missing $path" >&2; fail=1; continue; }
        got=$(sha "$DEST/$path")
        [ "$got" = "$want" ] || { echo "vendor: $path does not match the manifest" >&2; fail=1; }
    done < "$DEST/MANIFEST.sha256"
    [ "$fail" = "0" ] || exit 1
    echo "vendor: EmulatorJS $VERSION present and intact"
    exit 0
fi

command -v curl >/dev/null 2>&1 || { echo "vendor: curl not found" >&2; exit 1; }

rm -rf "$DEST"
mkdir -p "$DEST/cores" "$DEST/cores/reports" "$DEST/compression" "$DEST/localization"

echo "vendor: fetching EmulatorJS $VERSION"
for f in $FILES; do
    printf '  %s' "$f"
    curl -fsSL "$BASE/$f" -o "$DEST/$f" || { echo " - FAILED"; exit 1; }
    printf ' (%s bytes)\n' "$(wc -c < "$DEST/$f" | tr -d ' ')"
done

printf '  LICENSE'
curl -fsSL "$LICENSE_URL" -o "$DEST/LICENSE" || { echo " - FAILED"; exit 1; }
printf ' (%s bytes)\n' "$(wc -c < "$DEST/LICENSE" | tr -d ' ')"

cat > "$DEST/SOURCE.txt" <<EOF
EmulatorJS $VERSION - the emulator this project's play page runs on.

NOT PART OF UR FINKEL.  UR FINKEL is MIT licensed.  These files are not:

  EmulatorJS            GPL-3.0
  vice_xplus4 core      VICE, GPL-2.0-or-later, built for EmulatorJS

They are included here, and in the downloadable collections, so that the
play page works with no network at all.  Bundling them alongside an MIT
program is mere aggregation: each half keeps its own licence.

The full GPL-3.0 text is in LICENSE beside this file.

CORRESPONDING SOURCE, as the GPL requires it be offered:

    https://github.com/EmulatorJS/EmulatorJS         (tag v$VERSION)
    https://github.com/libretro/vice-libretro        (the core)
    https://vice-emu.sourceforge.io/                 (VICE itself)

These files were downloaded verbatim from

    $BASE/

and were not modified.  MANIFEST.sha256 beside this file records the
SHA-256 of every one of them; tools/vendor-emulator.sh --check verifies
them, and re-running that script fetches them again from the same pinned
version.
EOF
printf '  SOURCE.txt\n'

# Written last, and deliberately not listing itself or the licence text: it
# records what was fetched, so that a later --check can tell a corrupted or
# quietly-updated file from an intact one.
: > "$DEST/MANIFEST.sha256"
for f in $FILES; do
    printf '%s %s\n' "$(sha "$DEST/$f")" "$f" >> "$DEST/MANIFEST.sha256"
done

echo "vendor: $(du -sh "$DEST" | cut -f1) in $DEST"
