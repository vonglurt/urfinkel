# UR FINKEL - build & run for the Commodore Plus/4 (compiled edition)
#
# Requires cc65 (brew install cc65) for the target build and VICE
# (brew install vice) for c1541 and the xplus4 emulator.  The host builds
# used by `make check` and `make music` need nothing but the system C
# compiler and python3.
#
# Resolve tools even when Homebrew is not on PATH (e.g. minimal shells).
BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
CL65   ?= $(shell command -v cl65   2>/dev/null || echo $(BREW_PREFIX)/bin/cl65)
PETCAT ?= $(shell command -v petcat 2>/dev/null || echo $(BREW_PREFIX)/bin/petcat)
C1541  ?= $(shell command -v c1541  2>/dev/null || echo $(BREW_PREFIX)/bin/c1541)
XPLUS4 ?= $(shell command -v xplus4 2>/dev/null || echo $(BREW_PREFIX)/bin/xplus4)
HOSTCC ?= cc
PYTHON ?= python3

# -Osir is cc65's full optimiser (optimise, inline known functions, use
# registers, inline more aggressively) and -Cl makes local variables
# static rather than stack-based.  Together they took the board draw from
# 151 ms to 123 ms; see docs/lab-report.md section VI-A.
# Stamped into the cabinet so a build can be identified from the machine -
# see stage_floor in front.c.  Date only: a time would change every build
# and make every .prg differ, which would defeat `make conform` telling you
# whether anything actually changed.
BUILD_DATE := $(shell date +%Y-%m-%d)

CC65FLAGS = -t plus4 -Osir -Cl -DBUILD_DATE='"$(BUILD_DATE)"'

BUILD  = build
SRC    = src
TOOLS  = tools

# Everything the game is made of, minus whichever main() is being linked.
CORE = $(SRC)/board.c $(SRC)/rules.c $(SRC)/text.c $(SRC)/dice.c \
       $(SRC)/urbot.c $(SRC)/music.c $(SRC)/front.c $(SRC)/kbd.c \
       $(SRC)/etch.c \
       $(SRC)/dbg.c $(SRC)/blit.s $(SRC)/irq.s
HDRS = $(SRC)/plus4.h $(SRC)/board.h $(SRC)/rules.h $(SRC)/text.h \
       $(SRC)/dice.h $(SRC)/urbot.h $(SRC)/music.h $(SRC)/front.h \
       $(SRC)/kbd.h $(SRC)/etch.h $(SRC)/dbg.h $(SRC)/song.h

GAME  = $(BUILD)/urfinkel.prg
DBGGAME = $(BUILD)/urfinkel-dbg.prg
DISK  = $(BUILD)/urfinkel.d64

# The frozen BASIC edition, which is both the conformance oracle and the
# other half of every benchmark comparison.
BASIC = ../urroyal-basic/urroyal.bas

VICEENV = XDG_DATA_DIRS=$(BREW_PREFIX)/share:$$XDG_DATA_DIRS

# Headless screenshots go through tools/viceshot.py rather than straight
# to xplus4.  VICE's autostart loses the RUN keystroke about one run in
# eight and there is no setting that makes it certain, so the harness
# verifies that the program actually ran and retries when it did not.
SHOT     = BREW_PREFIX=$(BREW_PREFIX) $(PYTHON) $(TOOLS)/viceshot.py
CYCLES   ?= 400000000
BASCYCLES ?= 900000000

all: $(GAME)

$(BUILD):
	mkdir -p $(BUILD)

# --- the game ------------------------------------------------------------
$(GAME): $(SRC)/game.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -o $@ $(SRC)/game.c $(CORE)

# --- the traced build ----------------------------------------------------
# The same sources and the same optimiser flags, plus -DDEBUG.  Keeping the
# flags identical matters: a debug build compiled differently from the
# production one is a different program, and the bugs worth chasing on this
# machine are the ones that move when the code moves.  Everything the trace
# costs is inside #ifdef DEBUG, so `make` and `make debug` differ by that
# one define and nothing else.
#
#   f1  overlay on/off   f2  filter a subsystem   f3  breadcrumb   f4  step
debug: $(DBGGAME)

$(DBGGAME): $(SRC)/game.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -DDEBUG -o $@ $(SRC)/game.c $(CORE)
	@echo "traced build: $@  (f1 overlay, f2 filter, f3 crumb, f4 step)"

# Boot the traced build headless and screenshot the trace panel.  This is
# the tool for a machine that has STOPPED: the panel is the last dozen
# things the program said it was doing, and it is still on the screen.
debug-shot: $(DBGGAME)
	$(SHOT) $(DBGGAME) $(BUILD)/debug.png $(DBGCYCLES) --min-black=0.0
	@echo "trace panel: $(BUILD)/debug.png"

DBGCYCLES ?= 900000000

$(BUILD)/demo.prg: $(SRC)/demo.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -o $@ $(SRC)/demo.c $(CORE)

$(BUILD)/bench.prg: $(SRC)/bench.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -o $@ $(SRC)/bench.c $(CORE)

# --- music ---------------------------------------------------------------
# Songs are written as strings in tools/songs.mml and compiled on the host
# into src/song.h.  Editing the notation and running `make music` is the
# whole authoring loop; nothing about the player changes.
# --- the music -----------------------------------------------------------
# Two sources feed one table.  tools/songs.mml is hand-written and holds
# the CUES - the victory theme (which is the BASIC edition's note for note
# and must not move), the fanfare, the two turn cheers, the intro, the
# select chime and the riser.  The BEDS are transcribed from assets/midi by
# tools/midibed.py, which is a faithful conversion and not an arrangement:
# both voices keep their own part at its own note values.
#
# MIDBUDGET is the ceiling on transcribed song data, in bytes.  midibed.py
# packs sources in until the next one will not fit and reports what it
# dropped, so adding a .mid to assets/midi never silently overflows the
# machine - it either fits or it is named in the build log.
#
# The number is not a guess: `make music-budget` prints what the last link
# left free, and this is that figure less a margin.  Raise it only after
# re-running that.
#
# IT GOES DOWN AS THE CODE GOES UP, and that is the whole relationship.
# This is the program's shock absorber: the beds take whatever the code
# does not, so the cost of a feature is paid in songs and is visible here
# rather than as a link failure with no explanation.  2026-08-08: 23400 ->
# 22200, buying 1525 bytes for the apron effects (backlog 7.6) at the cost
# of three pieces out of the rotation, 21 kept down to 19.  The link had
# SEVEN bytes free before this was lowered; it has 364 now, which is thin
# enough that the next feature should expect to lower this again.
#
# What it does NOT yet pay for is `make debug`, which wants ~1.6 KB more
# than exists and has not linked for some time - see backlog 15.18.
MIDBUDGET ?= 22200
MIDS  = $(wildcard assets/midi/*.mid)
MIDMML = $(TOOLS)/songs-midi.mml

music: $(SRC)/song.h

$(MIDMML) $(SRC)/song_beds.h: $(TOOLS)/midibed.py $(TOOLS)/mml.py $(MIDS)
	PYTHONPATH=$(TOOLS) $(PYTHON) $(TOOLS)/midibed.py $(MIDS) \
		--budget=$(MIDBUDGET) --out=$(MIDMML) --beds=$(SRC)/song_beds.h

$(SRC)/song.h: $(TOOLS)/mml.py $(TOOLS)/songs.mml $(MIDMML)
	$(PYTHON) $(TOOLS)/mml.py $(TOOLS)/songs.mml $(MIDMML) $@

# What the last link actually left free, which is the only honest input to
# MIDBUDGET above.  BSS is the top of everything the program occupies; the
# ceiling is HIMEM less the 2K C stack, both from cc65's plus4.cfg.
music-budget: $(GAME)
	@$(CL65) $(CC65FLAGS) -m $(BUILD)/urfinkel.map -o $(BUILD)/_b.prg \
		$(SRC)/game.c $(CORE) >/dev/null 2>&1 || true
	@$(PYTHON) -c "import re;m=open('$(BUILD)/urfinkel.map').read();\
b=re.search(r'BSS\s+(\w+)\s+(\w+)',m);t=int(b.group(2),16)+1;c=0xFD00-0x0800;\
print('top of BSS   \$$%04X' % t);print('ceiling      \$$%04X' % c);\
print('FREE         %d bytes' % (c-t));\
print('MIDBUDGET is $(MIDBUDGET); it could be %d' % ($(MIDBUDGET)+(c-t)-1024))"

# --- the keyboard probe --------------------------------------------------
# A bench instrument for the one link in the keyboard path that cannot be
# tested from the host: the KERNAL's interrupt scanning the keys and
# filling $EF.  VICE's -keybuf writes that byte directly, so a headless
# test jumps straight over the only part that might be broken.  The
# procedure, and what each outcome means, is in backlog.md section C.
kbtest: $(BUILD)/kbtest.prg

$(BUILD)/kbtest.prg: $(SRC)/kbtest.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -o $@ $(SRC)/kbtest.c $(CORE)

# The host-testable half of the same question.  kbtest needs a human
# holding a key down; this needs nobody, because it watches the KERNAL's
# jiffy clock rather than its keyboard buffer - the same interrupt advances
# both, and the clock can be observed without -keybuf writing $EF by hand
# and thereby jumping over the only stage under suspicion.  A screenshot of
# it carries the verdict in words.
kbdiag: $(BUILD)/kbdiag.prg

$(BUILD)/kbdiag.prg: $(SRC)/kbdiag.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -o $@ $(SRC)/kbdiag.c $(CORE)

kbdiag-shot: $(BUILD)/kbdiag.prg
	$(SHOT) $(BUILD)/kbdiag.prg $(BUILD)/kbdiag.png 120000000
	@echo "verdict: $(BUILD)/kbdiag.png"

# The human half: a prompt to type at, with the KERNAL route and the TED
# route shown side by side.  Boots in a real emulator window because the
# whole point is that somebody presses a key.
kbtype: $(BUILD)/kbtype.prg

$(BUILD)/kbtype.prg: $(SRC)/kbtype.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -o $@ $(SRC)/kbtype.c $(CORE)

kbtype-run: $(BUILD)/kbtype.prg
	$(VICEENV) $(XPLUS4) -autostart $(BUILD)/kbtype.prg

# Build the decode table: asks for 1 2 3 4 5 m one at a time and records
# the matrix signature of each, under all three candidate read protocols.
kbhunt: $(BUILD)/kbhunt.prg

$(BUILD)/kbhunt.prg: $(SRC)/kbhunt.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -o $@ $(SRC)/kbhunt.c $(CORE)

kbhunt-run: $(BUILD)/kbhunt.prg
	$(VICEENV) $(XPLUS4) -autostart $(BUILD)/kbhunt.prg

# Put the probe on the card next to the game, in the dev folder only - the
# root stays the game, so DLOAD"*" still finds the right thing.
# All three keyboard instruments, because the one you need is the one you
# find out you need while sitting at the machine with the Mac in another
# room.  kbdiag prints the idle matrix, which is the single reading that
# matters on real hardware: kbd_init permanently ignores any line that is
# low at boot, and on the emulator nothing ever is.
card-probe: $(BUILD)/kbtest.prg $(BUILD)/kbtype.prg $(BUILD)/kbdiag.prg
	@test -d "$(CARD)" || { echo "no card mounted at $(CARD)"; exit 1; }
	mkdir -p "$(CARDDIR)"
	COPYFILE_DISABLE=1 cp $(BUILD)/kbtest.prg "$(CARDDIR)/kbtest.prg"
	COPYFILE_DISABLE=1 cp $(BUILD)/kbtype.prg "$(CARDDIR)/kbtype.prg"
	COPYFILE_DISABLE=1 cp $(BUILD)/kbdiag.prg "$(CARDDIR)/kbdiag.prg"
	rm -f "$(CARDDIR)"/._*
	sync
	@cmp "$(CARDDIR)/kbtest.prg" $(BUILD)/kbtest.prg
	@cmp "$(CARDDIR)/kbtype.prg" $(BUILD)/kbtype.prg
	@cmp "$(CARDDIR)/kbdiag.prg" $(BUILD)/kbdiag.prg
	@echo "probes on the card (verified): kbtest, kbtype, kbdiag"
	@echo "on the machine:  open15,8,15,\"cd:dev\":close15 : dload\"kbdiag\" : run"

# --- the animation gallery -----------------------------------------------
# Every moving thing in this game is wired into a moment - the trophy only
# pours when somebody wins - which makes them expensive to look at and hard
# to compare.  These build each one on its own, chosen with -DANIM, so a
# screenshot of it is deterministic and the whole set can be laid out as
# one contact sheet.
#
#   make anim              build/anim-sheet.png, all of them
#   make anim-run ANIM=2   one of them, in the emulator, to watch
ANIM ?= 0

$(BUILD)/anim%.prg: $(SRC)/anim.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) -DANIM=$* -o $@ $(SRC)/anim.c $(CORE)

anim-run: | $(BUILD)
	$(CL65) $(CC65FLAGS) -DANIM=$(ANIM) -o $(BUILD)/anim.prg \
		$(SRC)/anim.c $(CORE)
	$(VICEENV) $(XPLUS4) -autostart $(BUILD)/anim.prg

anim:
	BREW_PREFIX=$(BREW_PREFIX) $(PYTHON) $(TOOLS)/anim.py
	@echo "the gallery: $(BUILD)/anim-sheet.png"

# --- host-side rule tests ------------------------------------------------
# rules.c has no Plus/4 in it, so the ruleset can be tested with the system
# compiler in milliseconds instead of a minute of emulation.  This is the
# fast inner loop for rules work; the on-target self-test stays the final
# word, because only it exercises the real machine.
check: $(BUILD)/test_rules $(BUILD)/test_kbd
	./$(BUILD)/test_rules
	./$(BUILD)/test_kbd

$(BUILD)/test_rules: test/test_rules.c $(SRC)/rules.c $(SRC)/rules.h | $(BUILD)
	$(HOSTCC) -std=c99 -Wall -Wextra -O2 -I$(SRC) -o $@ \
		test/test_rules.c $(SRC)/rules.c

# The keyboard DECODER, on the host.  kbd.c has no Plus/4 in it except one
# function - the eight-byte matrix read - and KBD_FAKE_MATRIX swaps that
# one out, so what the bytes MEAN can be tested here in milliseconds.  What
# the bytes ARE stays a bench question: see the note at the top of the test
# and docs/keyboard-report.md section III on why faking the stage under
# suspicion is how the KERNAL route passed every test while being dead.
$(BUILD)/test_kbd: test/test_kbd.c $(SRC)/kbd.c $(SRC)/kbd.h | $(BUILD)
	$(HOSTCC) -std=c99 -Wall -Wextra -O2 -I$(SRC) -DKBD_FAKE_MATRIX -o $@ \
		test/test_kbd.c $(SRC)/kbd.c

# --- run on the emulator -------------------------------------------------
run: $(DISK)
	$(VICEENV) $(XPLUS4) -autostart $(DISK)

# Watch it at TSPEED percent - a whole URBOT demo in a fraction of the time.
TSPEED ?= 200
run200: $(DISK)
	$(VICEENV) $(XPLUS4) -speed $(TSPEED) -autostart $(DISK)

# --- smoke test ----------------------------------------------------------
# Boot the game, let the attract timeout start the URBOT demo on its own,
# and screenshot whatever is on screen a few emulated minutes later.
# A short attract timeout so the demo starts almost at once - the point of
# the smoke test is that a game plays, not that the menu can wait.
SMOKEFLAGS ?= -DATTRACT_FRAMES=250

test: $(BUILD)/smoke.png
$(BUILD)/smoke.prg: $(SRC)/game.c $(CORE) $(HDRS) | $(BUILD)
	$(CL65) $(CC65FLAGS) $(SMOKEFLAGS) -o $@ $(SRC)/game.c $(CORE)

# Judged on --max-white, not on the usual --min-black.  The smoke test is
# the one shot taken while the program is still PLAYING, and a live match
# is legitimately not black: turn_sweep floods the background for 72 frames
# every time the players change, and 600e6 cycles lands inside one.  The
# black test threw that healthy frame away six times running, because VICE
# under -warp reproduces the same frame on every retry.  So this one tests
# for the failure - the BASIC boot screen, which is 57% pure white against
# under 3% for any running program - rather than for a proxy of success.
# --still-at shoots a SECOND frame and requires the two to differ.  Without
# it this gate passed for hours on a board that had been frozen since 360e6
# cycles: a wedged game board is still a game board, so every predicate that
# asks "what is on screen" says yes.  The only question that catches a
# stopped machine is whether anything is still moving.
$(BUILD)/smoke.png: $(BUILD)/smoke.prg
	$(SHOT) $(BUILD)/smoke.prg $@ 600000000 --max-white=0.25 \
		--still-at=900000000
	@echo "smoke test ok - and still moving at 900e6: $@"

# --- the migration measurement -------------------------------------------
# Both halves of the comparison, each a screenshot of the machine reporting
# its own timings.  Read build/bench.png next to build/basicbench.png.
bench: $(BUILD)/bench.png $(BUILD)/basicbench.png
	@echo "compiled: $(BUILD)/bench.png"
	@echo "basic   : $(BUILD)/basicbench.png"

$(BUILD)/bench.png: $(BUILD)/bench.prg
	$(SHOT) $(BUILD)/bench.prg $@ $(CYCLES)

# The BASIC baseline is built by patching a throwaway copy of the frozen
# source: line 300 (the head of the stage intro) becomes a jump into an
# appended benchmark block, so the boot-time tables are built and nothing
# else runs.  The frozen edition itself is never modified.
$(BUILD)/basicbench.prg: $(BASIC) bench/basic-bench.bas | $(BUILD)
	sed 's/^300 .*/300 goto 9700/' $(BASIC) > $(BUILD)/basicbench.bas
	cat bench/basic-bench.bas >> $(BUILD)/basicbench.bas
	$(PETCAT) -w3 -o $@ -- $(BUILD)/basicbench.bas

$(BUILD)/basicbench.png: $(BUILD)/basicbench.prg
	$(SHOT) $(BUILD)/basicbench.prg $@ $(BASCYCLES)

# --- renderer regression -------------------------------------------------
# WHAT THIS USED TO BE, AND WHAT IT IS NOW.  READ THIS BEFORE TRUSTING IT.
#
# Until 2026-08-07 this diffed the compiled board against the FROZEN BASIC
# EDITION's, byte for byte, and that was worth far more than it looks: it
# was an oracle written by a different program, so it could catch the port
# being wrong in ways the port's own author had not thought to check.  It
# earned that twice over - cc65's startup leaving the machine in the
# lower-case character set, and the TED background never being set to
# black - neither of which is a logic error and neither of which any rule
# test would have found.
#
# The board then grew two rows for the black separators between bands, and
# a taller board cannot be pixel-identical to a twelve-row one.  The oracle
# was retired deliberately rather than quietly, and what replaces it is
# strictly weaker: a GOLDEN SNAPSHOT of our own renderer.  It answers "has
# the board changed since somebody last looked at it and approved it",
# which is a regression test.  It does NOT answer "does the board still
# match the specification", because it is no longer compared against
# anything the specification produced.
#
# So: when this fails, LOOK AT THE TWO IMAGES.  Re-baselining is now a
# judgement, not a formality, and `make conform-bless` is deliberately a
# separate command you have to type on purpose.
GOLDEN = test/golden-board.png

conform: $(BUILD)/demo.png
	@test -f $(GOLDEN) || { echo "no golden board - see 'make conform-bless'"; exit 1; }
	@cmp $(BUILD)/demo.png $(GOLDEN) \
		&& echo "conform ok: the board is unchanged from the approved one" \
		|| { echo "*** the board has changed ***"; \
		     echo "    approved: $(GOLDEN)"; \
		     echo "    now:      $(BUILD)/demo.png"; \
		     echo "    look at both.  If the change is wanted: make conform-bless"; \
		     exit 1; }

# Approve the current board as the new reference.  Separate, and named so
# that doing it by accident is difficult.
conform-bless: $(BUILD)/demo.png
	cp $(BUILD)/demo.png $(GOLDEN)
	@echo "approved as the reference board: $(GOLDEN)"

# The frozen edition's board, still buildable, because the comparison is
# worth making by eye even though it is no longer an equality.
basicboard: $(BUILD)/basicboard.png
	@echo "the frozen edition's board: $(BUILD)/basicboard.png"

$(BUILD)/demo.png: $(BUILD)/demo.prg
	$(SHOT) $(BUILD)/demo.prg $@ 200000000

$(BUILD)/basicboard.prg: $(BASIC) bench/basic-board.bas | $(BUILD)
	sed 's/^300 .*/300 goto 9700/' $(BASIC) > $(BUILD)/basicboard.bas
	cat bench/basic-board.bas >> $(BUILD)/basicboard.bas
	$(PETCAT) -w3 -o $@ -- $(BUILD)/basicboard.bas

$(BUILD)/basicboard.png: $(BUILD)/basicboard.prg
	$(SHOT) $(BUILD)/basicboard.prg $@ $(BASCYCLES)

# --- deploy --------------------------------------------------------------
# The d64 is the boot form for both VICE (`make run`) and the SD2IEC, and
# it is a 1541 image rather than a d81 because every drive and every
# emulator reads one without argument.  The loose .prg is carried
# alongside it because DLOAD"*" takes the first program in a directory,
# which is the shortest thing to type on the machine.
disk: $(DISK)

$(DISK): $(GAME)
	$(C1541) -format "ur finkel,uf" d64 $(DISK) -write $(GAME) urfinkel

# Sync to the SD2IEC card: the DEV folder for working builds, and the card
# ROOT as well, so the game is reachable without a CD.  Both the .prg and
# the .d64 go to both places - the .prg for DLOAD, the .d64 for anything
# that wants a real disk (and for mounting straight into VICE off the
# card).  The .prg is written FIRST on purpose: DLOAD"*" takes the first
# program in the directory.
#
# Every name on the card is lowercase and <=16 characters.  That is not
# cosmetic: the SD2IEC converts FAT names (ASCII) to PETSCII, and
# uppercase-ASCII names come back as awkward glyphs that are hard to read
# and hard to type on a Plus/4 that boots in upper/graphics mode.  Long
# names beyond 16 characters fall back to unpredictable DOS 8.3
# shortnames, so they stay short too.
#
# COPYFILE_DISABLE stops macOS writing ._ AppleDouble companions, which
# would otherwise show up as junk entries in the DIRECTORY listing.
CARD    ?= /Volumes/MSDOSFAT
CARDDIR  = $(CARD)/dev

# THE DATED SNAPSHOT.  Every sync also drops the pair into dev/<stamp>/,
# so the card carries a history rather than only the newest build and a
# regression can be compared against the thing it regressed from without
# a rebuild.  The stamp is yymmddhhmmss - twelve digits, which fits the
# SD2IEC's sixteen-character name limit with room to spare, so the full
# seconds resolution is affordable and two builds in one hour do not
# collide.
#
# It is taken from the .prg's MTIME rather than from `date`, so the stamp
# names the BUILD and not the sync: running `make card` three times over
# one binary refills one folder instead of littering three.  `=` and not
# `:=` because $(GAME) does not exist yet when make parses this file.
STAMP    = $(shell date -r $(GAME) +%y%m%d%H%M%S)
CARDSNAP = $(CARDDIR)/$(STAMP)

card: $(GAME) $(DISK)
	@test -d "$(CARD)" || { echo "no card mounted at $(CARD)"; exit 1; }
	mkdir -p "$(CARDDIR)" "$(CARDSNAP)"
	COPYFILE_DISABLE=1 cp $(GAME) "$(CARDDIR)/urfinkel.prg"
	COPYFILE_DISABLE=1 cp $(DISK) "$(CARDDIR)/urfinkel.d64"
	COPYFILE_DISABLE=1 cp $(GAME) "$(CARD)/urfinkel.prg"
	COPYFILE_DISABLE=1 cp $(DISK) "$(CARD)/urfinkel.d64"
	COPYFILE_DISABLE=1 cp $(GAME) "$(CARDSNAP)/urfinkel.prg"
	COPYFILE_DISABLE=1 cp $(DISK) "$(CARDSNAP)/urfinkel.d64"
	rm -f "$(CARDDIR)"/._* "$(CARD)"/._* "$(CARDSNAP)"/._*
	sync
	@cmp "$(CARDDIR)/urfinkel.prg" $(GAME)
	@cmp "$(CARDDIR)/urfinkel.d64" $(DISK)
	@cmp "$(CARD)/urfinkel.prg"    $(GAME)
	@cmp "$(CARD)/urfinkel.d64"    $(DISK)
	@cmp "$(CARDSNAP)/urfinkel.prg" $(GAME)
	@cmp "$(CARDSNAP)/urfinkel.d64" $(DISK)
	@echo "card sync ok (verified):"
	@echo "  $(CARD)/urfinkel.prg   $(CARD)/urfinkel.d64"
	@echo "  $(CARDDIR)/urfinkel.prg   $(CARDDIR)/urfinkel.d64"
	@echo "  $(CARDSNAP)/urfinkel.prg   $(CARDSNAP)/urfinkel.d64"
	@echo "on the machine, easiest first:"
	@echo "  press NEXT on the SD2IEC to mount urfinkel.d64, then dload\"*\" : run"
	@echo "  or:  dload\"urfinkel\" : run"
	@echo "  or:  open15,8,15,\"cd:dev\":close15:dload\"*\" : run"
	@echo "  this build's snapshot: cd:dev then cd:$(STAMP)"

card-eject:
	sync
	diskutil eject "$(CARD)"

clean:
	rm -rf $(BUILD)

.PHONY: all debug debug-shot kbtest card-probe music check run run200 test \
        anim anim-run \
        bench conform disk card card-eject clean kbdiag kbdiag-shot
