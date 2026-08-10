/* ------------------------------------------------------------------------
 * kbd.c - the TED keyboard scanner.  See kbd.h for why this replaces the
 * KERNAL, and docs/keyboard-report.md for the measurement that proved it
 * had to.
 * --------------------------------------------------------------------- */

#include "kbd.h"

/* TED's keyboard latch, and the row-select latch the mask is written to.
** Both volatile: the entire point of them is that they change underneath
** the program, and -Osir will hoist a non-volatile read straight out of a
** scan loop. */
#define KBD_SEL   (*(volatile unsigned char*)0xFD30)
#define KBD_LATCH (*(volatile unsigned char*)0xFF08)

/* The Plus/4 matrix: keymap[row][bit].  Rows are what gets pulled low at
** $FD30, bits are what comes back from $FF08.  Zero means "this key has no
** meaning to this program" - the game never asks about C= or CTRL, and a
** key that maps to zero is simply not reported.
**
** Codes are chosen to be what game.c already tests for: 13 return, 20
** delete, ' ' space, plain lower-case letters and digits (keyis() puts
** both cases through scr_code, so lower case is enough), and 133/137/134/
** 138 for f1-f4 because dbg.h expects exactly those. */
static const unsigned char keymap[8][8] = {
    /* row 0: DEL RET  £    HELP F1   F2   F3   @        */
    {  20,  13,  0,   138, 133, 137, 134, '@'  },
    /* row 1: 3   W    A    4    Z    S    E    SHIFT    */
    { '3', 'w', 'a', '4', 'z', 's', 'e',  0    },
    /* row 2: 5   R    D    6    C    F    T    X        */
    { '5', 'r', 'd', '6', 'c', 'f', 't', 'x'  },
    /* row 3: 7   Y    G    8    B    H    U    V        */
    { '7', 'y', 'g', '8', 'b', 'h', 'u', 'v'  },
    /* row 4: 9   I    J    0    M    K    O    N        */
    { '9', 'i', 'j', '0', 'm', 'k', 'o', 'n'  },
    /* row 5: DN  P    L    UP   .    :    -    ,        */
    {  17, 'p', 'l', 145, '.', ':', '-', ','  },
    /* row 6: LF  *    ;    RT   ESC  =    +    /        */
    { 157, '*', ';',  29,  27, '=', '+', '/'  },
    /* row 7: 1   HOME CTRL 2    SPC  C=   Q    STOP     */
    { '1',  19,  0,  '2', ' ',  0,  'q',  3   },
};

static unsigned char prev[8];           /* what was down two scans running */
static unsigned char last[8];           /* what the previous scan saw      */
static unsigned char ignore[8];         /* lines that are low when idle    */

/* Keys that have been pressed but not yet collected.  A ring, because the
** scan runs at 50 Hz and the game collects whenever its current loop next
** comes round - which during a curtain or a throw can be a good many
** frames later.  Without somewhere to put them, every key pressed during
** an animation would be dropped on the floor. */
#define KBUF 8
static unsigned char kbuf[KBUF];
static unsigned char khead, ktail;

/* The row mask goes to $FD30, a write to $FF08 latches that row's column
** lines, and reading $FF08 gives them back active low.  This is the
** protocol kbhunt measured on the machine, and it is deliberately read
** ONCE: an earlier version read twice and ORed the two together, which
** cannot filter anything a latched register can do to you and only adds a
** way to lose a real press.  Bounce is handled below, in time.
** KBD_FAKE_MATRIX replaces this one function, and nothing else, so that
** test/test_kbd.c can drive the decode and the debounce on the host.  That
** is the half of the keyboard path a host test can honestly cover: what
** the eight bytes MEAN.  What they are is hardware, and only kbhunt on a
** real machine can say - see docs/keyboard-report.md §III on why a test
** that fakes the stage under suspicion proves nothing. */
#ifdef KBD_FAKE_MATRIX
extern void kbd_fake_scan (unsigned char* out);
#  define raw_scan kbd_fake_scan
#else
static void raw_scan (unsigned char* out)
{
    unsigned char i;

    for (i = 0; i < 8; ++i) {
        KBD_SEL   = (unsigned char)~(1 << i);
        KBD_LATCH = 0xFF;
        out[i] = KBD_LATCH;
    }
}
#endif

/* Learn the idle matrix once, and treat anything already low as not a key.
**
** This is not defensive programming for its own sake.  The first version of
** the hunt probe read 127 ($7F, bit 7 low) with nothing held at all, which
** made every "is a key down?" test true forever and produced a table of
** noise.  Whether that was a genuinely stuck line or an artefact of reading
** without selecting a row first, a scanner that calibrates against idle
** cannot be fooled by it either way - and if nothing is stuck, this costs
** one scan at boot and masks nothing.  On the emulator it masks nothing:
** `make kbdiag-shot` prints the idle matrix and it reads 255 eight times. */
void kbd_init (void)
{
    unsigned char i;

    raw_scan (ignore);
    for (i = 0; i < 8; ++i) { prev[i] = 0; last[i] = 0; }
    khead = ktail = 0;
}

/* ONE SCAN, ONE FRAME.  The rate is the whole point of this function
** existing separately from kbd_get.
**
** The debounce below asks a key to read down on two consecutive SCANS, and
** that is only a length of time if something fixes how often a scan
** happens.  Nothing used to: kbd_get did its own scanning and was called
** from wherever the game happened to be, so
**
**     wait_key   spun on poll_key with no frame wait at all, scanning
**                thousands of times a second - two consecutive scans a few
**                microseconds apart, which debounces nothing, and is where
**                the phantom keypresses came from
**     menu       called poll_key once per loop, and its loop is four
**                frames of waiting plus a lamp chase - two consecutive
**                scans a THIRD OF A SECOND apart, so an ordinary tap was
**                never seen twice and never arrived at all
**
** Same code, four orders of magnitude apart, and both failure modes are
** the same mistake: a debounce counted in calls rather than in time.
**
** So the scan is driven from wait_frames_live, once a frame, and from
** nowhere else.  Every wait in the game goes through there, which makes
** the rate 50 Hz everywhere and the debounce a flat 40 ms - long enough to
** outlast contact bounce, short enough that no human tap can hide inside
** it. */
void kbd_scan (void)
{
    unsigned char cur[8], down, fresh;
    unsigned char i, b, raw, k;

    raw_scan (cur);

    for (i = 0; i < 8; ++i) {
        /* Active low, with the idle-low lines forced back high, then
        ** inverted so a 1 bit means "this key is down". */
        raw   = (unsigned char)~(cur[i] | (unsigned char)~ignore[i]);

        down  = (unsigned char)(raw & last[i]);
        last[i] = raw;

        fresh = (unsigned char)(down & (unsigned char)~prev[i]);
        prev[i] = down;

        for (b = 0; fresh && b < 8; ++b) {
            if (!(fresh & (unsigned char)(1 << b))) continue;
            k = keymap[i][b];
            if (!k) continue;
            /* A full buffer drops the newest rather than the oldest: the
            ** keys already waiting are the ones the player pressed first
            ** and expects to see act first. */
            if ((unsigned char)((khead + 1) % KBUF) == ktail) continue;
            kbuf[khead] = k;
            khead = (unsigned char)((khead + 1) % KBUF);
        }
    }
}

unsigned char kbd_get (void)
{
    unsigned char k;

    if (khead == ktail) return 0;
    k = kbuf[ktail];
    ktail = (unsigned char)((ktail + 1) % KBUF);
    return k;
}
