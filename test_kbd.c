/* ------------------------------------------------------------------------
 * test_kbd.c - the keyboard decoder, run on the host.
 *
 * WHAT THIS COVERS, AND WHAT IT DELIBERATELY DOES NOT.
 *
 * `docs/keyboard-report.md` §III makes the case that a headless keyboard
 * test which fakes the stage under suspicion proves nothing, and it is
 * right: that is exactly how `-keybuf` writing `$EF` by hand made every
 * KERNAL-route test pass while no key could actually arrive.
 *
 * So this test fakes ONE function - the eight-byte matrix read - and
 * nothing else, and it makes no claim about the hardware.  What it does
 * claim is the other half, which had no test at all:
 *
 *     given these eight bytes, does the right key come out, once?
 *
 * That half is pure logic, it is where the bug that made the menu
 * unresponsive actually lived, and a regression in it is silent - the game
 * simply stops answering, exactly as it did.  The hardware half stays with
 * `make kbhunt` and a human on the bench.
 *
 * The scan rate is the debounce (see kbd_scan), so `scans` here stands for
 * frames: one call to kbd_scan is one frame at 50 Hz.
 *
 * Build and run with `make check`.
 * --------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>
#include "kbd.h"

/* --- the fake matrix, active low: a 0 bit is a key held ---------------- */

static unsigned char fake[8];

void kbd_fake_scan (unsigned char* out)
{
    memcpy (out, fake, 8);
}

static void release_all (void)
{
    memset (fake, 0xFF, sizeof (fake));
}

static void hold (unsigned char row, unsigned char bit)
{
    fake[row] &= (unsigned char)~(1u << bit);
}

static void release (unsigned char row, unsigned char bit)
{
    fake[row] |= (unsigned char)(1u << bit);
}

/* Start each case from a known machine: nothing held, nothing masked. */
static void fresh_start (void)
{
    release_all ();
    kbd_init ();
}

static void scans (int n)
{
    while (n-- > 0) kbd_scan ();
}

/* --- the harness ------------------------------------------------------- */

static int fails;

static void check (const char* what, long got, long want)
{
    if (got == want) {
        printf ("  pass %s\n", what);
    } else {
        printf ("  FAIL %s: got %ld, want %ld\n", what, got, want);
        ++fails;
    }
}

/* --- the six signatures kbhunt measured on the machine ----------------
** docs/keyboard-report.md §VI-A.  Carried across verbatim, for the same
** reason test_rules carries the frozen edition's rule table: a measurement
** written down in one place and asserted in another is the only kind that
** can catch a table being edited wrongly later.  The two the report did
** not press - 6 and 7 - come from the same documented rows and are
** included because the menu offers 6 and the player will try 7. */
struct sigcase {
    const char*   name;
    unsigned char row, bit, want;
};

static const struct sigcase sigs[] = {
    { "1", 7, 0, '1' },
    { "2", 7, 3, '2' },
    { "3", 1, 0, '3' },
    { "4", 1, 3, '4' },
    { "5", 2, 0, '5' },
    { "6", 2, 3, '6' },
    { "7", 3, 0, '7' },
    { "m", 4, 4, 'm' },
    { "return", 0, 1, 13 },
    { "delete", 0, 0, 20 },
    { "space",  7, 4, ' ' },
};
#define NSIGS (sizeof (sigs) / sizeof (sigs[0]))

int main (void)
{
    unsigned int i;
    unsigned char k;

    puts ("keyboard decoder checks\n");

    /* --- every key the menu and the name prompt need --------------- */
    puts (" the matrix, key by key");
    for (i = 0; i < NSIGS; ++i) {
        char label[32];

        fresh_start ();
        hold (sigs[i].row, sigs[i].bit);
        scans (2);                      /* down on two frames = debounced */

        snprintf (label, sizeof (label), "%s decodes", sigs[i].name);
        check (label, kbd_get (), sigs[i].want);
    }

    /* --- the debounce is a length of time -------------------------- */
    puts ("\n the debounce");

    fresh_start ();
    hold (7, 0);
    scans (1);                          /* one frame only */
    release_all ();
    scans (1);
    check ("a one-frame blip is rejected", kbd_get (), 0);

    fresh_start ();
    hold (7, 0);
    scans (2);
    check ("two frames is accepted", kbd_get (), '1');

    /* --- edge triggered, not level --------------------------------- */
    puts ("\n holding a key");

    fresh_start ();
    hold (7, 0);
    scans (50);                         /* a full second on the key */
    check ("a held key arrives once", kbd_get (), '1');
    check ("...and not again", kbd_get (), 0);

    release (7, 0);
    scans (2);
    hold (7, 0);
    scans (2);
    check ("release and press again repeats it", kbd_get (), '1');

    /* --- buffering across an animation ------------------------------
    ** The menu waits four frames at a time and the curtain far longer,
    ** so a key pressed while the game is busy has to survive until it
    ** next asks.  Before the buffer existed it did not. */
    puts ("\n keys pressed while the game is busy");

    fresh_start ();
    hold (7, 0);  scans (2);  release (7, 0);  scans (2);   /* 1 */
    hold (7, 3);  scans (2);  release (7, 3);  scans (2);   /* 2 */
    hold (1, 0);  scans (2);  release (1, 0);  scans (2);   /* 3 */
    check ("first key kept",  kbd_get (), '1');
    check ("second key kept", kbd_get (), '2');
    check ("third key kept",  kbd_get (), '3');
    check ("then empty",      kbd_get (), 0);

    /* --- a full buffer must not corrupt or reorder ----------------- */
    fresh_start ();
    for (i = 0; i < 20; ++i) {
        hold (7, 0);  scans (2);  release (7, 0);  scans (2);
    }
    check ("overflow keeps the oldest", kbd_get (), '1');
    for (k = 0; kbd_get (); ) { if (++k > 60) break; }
    check ("overflow drains and stops", k <= 60, 1);

    /* --- keys the program has no use for --------------------------- */
    puts ("\n keys with no meaning here");

    fresh_start ();
    hold (7, 5);                        /* the C= key: keymap holds 0 */
    scans (2);
    check ("an unmapped key reports nothing", kbd_get (), 0);

    /* --- the idle calibration, and the hazard it carries ------------
    ** A line low at boot is masked for the whole run.  On the emulator
    ** the idle matrix is a clean 255 eight times over, so this masks
    ** nothing - but if it ever does, the keys it removes go silently,
    ** and this case is here so that behaviour is at least written down
    ** and would change loudly. */
    puts ("\n idle calibration");

    release_all ();
    fake[7] &= (unsigned char)~1u;      /* row 7 bit 0 stuck low at boot */
    kbd_init ();
    scans (2);
    check ("a line stuck at boot is ignored", kbd_get (), 0);

    release_all ();
    kbd_init ();
    hold (7, 0);
    scans (2);
    check ("...and a clean boot leaves it working", kbd_get (), '1');

    printf ("\n%s\n", fails ? "*** keyboard checks FAILED ***"
                            : "all keyboard checks passed");
    return fails ? 1 : 0;
}
