#!/bin/sh
#
# Cut a stable release: `make release`, optionally RELNOTES=<file>.
#
# ONE PASS, IN ONE ORDER, AND NOTHING WRITTEN BACK.  Every hash a release
# publishes is computed here exactly once, from exactly the files that are
# published, and none of it goes back into the repository - so nothing it
# hashed can change after it hashed it.  That is the whole point of the
# script.  Before it existed the offline zip was committed on main, carried
# the play page, and was re-hashed every time the page was edited; a release
# uploaded from build/ once went out with a zip that did not match its notes.
#
#   1. refuse unless main is committed, pushed, and HEAD is what is on GitHub
#      - a release is of a commit anybody can check out;
#   2. the version is the stamp in the committed .prg - the one on the menu;
#   3. check out that commit afresh, in a worktree nobody has touched;
#   4. rebuild the .prg and .d64 there and require them byte-identical to the
#      committed ones;
#   5. build the zip there, twice, and require the two identical;
#   6. hash the three ONCE, into SHA256SUMS and MD5SUMS;
#   7. write the notes: the hand-written file, then the generated downloads;
#   8. sign the tag and push it.
#
# The result is build/release/<version>/, which `make publish` uploads.
#
# DRYRUN=1 does steps 1-7 and stops: no tag, no push.  It also skips the
# "already released" check, so the tools can be exercised on a version that
# has been released.
#
# The hand-written notes (RELNOTES, default docs/releases/next.md) must be
# committed, so the tag carries them.  __VERSION__ in them becomes the
# version.  Their first line, if it is a `# ` heading, is the release title.

set -e

die() { echo "release: $*" >&2; exit 1; }

notes=${1:-docs/releases/next.md}
dry=${DRYRUN:-0}
root=$(git rev-parse --show-toplevel)
cd "$root"

[ -f "$notes" ] || die "no notes at $notes - write the release's notes there first"
git diff --quiet && git diff --cached --quiet || die "the tree has uncommitted changes - commit them first"
git ls-files --error-unmatch "$notes" >/dev/null 2>&1 || die "$notes is not committed"

git fetch -q origin
head=$(git rev-parse HEAD)
[ "$head" = "$(git rev-parse origin/main)" ] ||
    die "HEAD is not origin/main - push first; a release is of what is on GitHub"

v=$(git show HEAD:build/urfinkel.prg | python3 -c "
import re,sys
m=re.search(rb'20\d\d\.\d\d\.\d{4}|20\d\d-\d\d-\d\d',sys.stdin.buffer.read())
sys.stdout.write(m.group().decode() if m else '')
")
[ -n "$v" ] || die "no version stamp in the committed build/urfinkel.prg"

if [ "$dry" != 1 ] && git rev-parse -q --verify "refs/tags/$v" >/dev/null; then
    [ "$(git rev-parse "$v^{commit}")" = "$head" ] ||
        die "tag $v already exists on another commit - is this version already released?"
fi

out=build/release/$v
rm -rf "$out"
mkdir -p "$out"

tmp=$(mktemp -d)
wt=$tmp/tree
cleanup() { git worktree remove --force "$wt" 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT

echo "release: $v - checking out $(git rev-parse --short HEAD) afresh"
git worktree add -q --detach "$wt" HEAD

# 4. The binaries: rebuilt from the commit, and they must be the committed ones.
(cd "$wt" && make clean >/dev/null && make BUILD_DATE="$v" >/dev/null &&
             make disk BUILD_DATE="$v" >/dev/null)
for f in urfinkel.prg urfinkel.d64; do
    git show "HEAD:build/$f" > "$tmp/$f.committed"
    cmp -s "$wt/build/$f" "$tmp/$f.committed" ||
        die "$f rebuilt from $v differs from the committed one"
done
echo "release: .prg and .d64 rebuild byte-identical to the commit"

# 5. The zip: built twice from a clean start, and it must come out the same.
(cd "$wt" && make dist BUILD_DATE="$v" >/dev/null)
cp "$wt/build/urfinkel.zip" "$tmp/zip.first"
(cd "$wt" && rm -rf build/dist build/urfinkel.zip && make dist BUILD_DATE="$v" >/dev/null)
cmp -s "$wt/build/urfinkel.zip" "$tmp/zip.first" || die "the zip does not reproduce"
echo "release: the zip reproduces"

cp "$wt/build/urfinkel.prg" "$wt/build/urfinkel.d64" "$wt/build/urfinkel.zip" "$out/"

# 6 and 7.  Hashed once; the notes take the hashes from these same bytes.
python3 - "$out" "$v" "$notes" <<'PY'
import hashlib, os, sys
out, v, notes = sys.argv[1:4]
files = ["urfinkel.prg", "urfinkel.d64", "urfinkel.zip"]
what = {"urfinkel.prg": "the program - starts fastest",
        "urfinkel.d64": "a disk image, for a floppy emulator or a real Plus/4",
        "urfinkel.zip": "the offline collection, with its own emulator"}
sums = {}
for f in files:
    b = open(os.path.join(out, f), "rb").read()
    sums[f] = (len(b), hashlib.sha256(b).hexdigest(), hashlib.md5(b).hexdigest())
with open(os.path.join(out, "SHA256SUMS"), "w") as h:
    h.writelines("%s  %s\n" % (sums[f][1], f) for f in files)
with open(os.path.join(out, "MD5SUMS"), "w") as h:
    h.writelines("%s  %s\n" % (sums[f][2], f) for f in files)
text = open(notes).read().replace("__VERSION__", v).rstrip() + "\n"
grp = lambda n: "{:,}".format(n).replace(",", " ")
text += "\n## Downloads\n\nThe menu shows **%s**, which is how this build identifies itself.\n\n" % v
text += "| File | | Bytes | SHA-256 |\n|---|---|---:|---|\n"
for f in files:
    text += "| %s | %s | %s | `%s` |\n" % (f, what[f], grp(sums[f][0]), sums[f][1])
text += ("\nSHA256SUMS and MD5SUMS are attached: save one beside the downloads and run "
         "`shasum -a 256 -c SHA256SUMS` or `md5sum -c MD5SUMS`.\n\n"
         "How to run them on a real Plus/4, VICE, YAPE or plus4emu is in "
         "[INSTALL.md](https://github.com/vonglurt/urfinkel/blob/%s/INSTALL.md).\n" % v)
open(os.path.join(out, "NOTES.md"), "w").write(text)
PY
echo "release: hashed once into $out/SHA256SUMS and MD5SUMS; notes in $out/NOTES.md"

if [ "$dry" = 1 ]; then
    echo "release: DRYRUN - stopping before the tag"
    exit 0
fi

# 8. The tag, signed, on the commit that was just rebuilt.
if ! git rev-parse -q --verify "refs/tags/$v" >/dev/null; then
    { echo "UR FINKEL $v"; echo; sed "s/__VERSION__/$v/g" "$notes"; } > "$tmp/tagmsg"
    git tag -s "$v" HEAD -F "$tmp/tagmsg"
fi
git tag -v "$v" >/dev/null 2>&1 || die "tag $v does not verify"
git push -q origin "refs/tags/$v"
echo "release: tag $v signed and pushed"
echo "release: done - upload it with \`make publish\`"
