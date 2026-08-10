/* ------------------------------------------------------------------------
 * kbtest.c - the keyboard probe.  A bench instrument, not a game.
 *
 * There is a link in the keyboard path that cannot be tested from the
 * host at all, and this exists to test it on the machine.
 *
 * A key press reaches the program in two stages:
 *
 *   1. the KERNAL's interrupt scans the keyboard matrix and puts the
 *      character in a buffer, bumping the count at $EF
 *   2. kbhit() reads $EF, and cgetc() takes the character
 *
 * Stage 2 can be tested headlessly, because VICE's -keybuf writes $EF
 * directly - and it passes.  Stage 1 cannot, for exactly the same reason:
 * writing $EF by hand jumps over the only part that might be broken.
 *
 * And there is a specific reason to suspect stage 1 here.  UR FINKEL
 * installs a raster interrupt for the music, taking over the vector at
 * $0314; interrupts that are not ours are passed on to the handler that
 * was there before.  If that hand-off is not working, the KERNAL never
 * scans the keyboard, $EF never moves, and no key is ever seen - which
 * looks exactly like "the keyboard does not work".
 *
 * So this program shows both stages at once, and switches the raster
 * interrupt on and off by itself every four seconds.  Nothing here is
 * driven by the keyboard, because the keyboard is what is on trial:
 * hold a key down and read the numbers.
 *
 *   frames    counts up always.  If it stops, the machine has stopped,
 *             and this is not a keyboard problem at all
 *   irq       counts up only while the raster interrupt is installed
 *   ndx       the KERNAL's buffer count, $EF.  Moves only if stage 1 works
 *   keys      how many characters this program has actually taken
 *   last      the code of the last one
 *
 * Build: make kbtest.  Copy: make card-probe.
 * --------------------------------------------------------------------- */

#include <conio.h>
#include "plus4.h"
#include "board.h"
#include "text.h"

/* The KERNAL's count of characters waiting in the keyboard buffer, which
** is what cc65's kbhit() reads (plus4.inc: KEY_COUNT := $EF). */
#define NDX     (*(volatile unsigned char*)0x00EF)

extern volatile unsigned char music_frames;  /* bumped by irq.s */
extern unsigned char scr_code (unsigned char c);
void irq_install (void);
void irq_remove (void);

#define SWAP_FRAMES     200             /* four seconds a side            */

static void put_num (unsigned char x, unsigned char y, unsigned int v,
                     unsigned char col)
{
    unsigned int  d = 10000;
    unsigned char i = 0;

    while (d) {
        rowtab[y][x + i] = (unsigned char)('0' + (unsigned char)(v / d));
        *(rowtab[y] + x + i - SCREEN_COLOR_D) = col;
        v %= d;
        d /= 10;
        ++i;
    }
}

static void frame (void)
{
    while (TED_RASTER_LO != 250) ;
    while (TED_RASTER_LO == 250) ;
}

void main (void)
{
    unsigned int  ticks = 0, nkeys = 0;
    unsigned char last = 0, irq_on = 1, phase = 0;

    cursor (0);
    board_init ();
    TED_BGCOLOR = CBYTE (0, 0);
    TED_BORDER  = CBYTE (2, 9);
    screen_fill (CH_SPACE, 0);

    text_centre (1, "ur finkel - keyboard probe", CBYTE (7, 7));

    text_put (4, 4,  "raster irq", CBYTE (5, 1));
    text_put (4, 6,  "frames",     CBYTE (5, 1));
    text_put (4, 7,  "irq",        CBYTE (5, 1));
    text_put (4, 9,  "ndx  ($ef)", CBYTE (5, 1));
    text_put (4, 10, "keys read",  CBYTE (5, 1));
    text_put (4, 11, "last code",  CBYTE (5, 1));

    text_put (2, 14, "hold a key down and watch ndx and", CBYTE (7, 1));
    text_put (2, 15, "keys read.  the raster irq turns",  CBYTE (7, 1));
    text_put (2, 16, "itself on and off every 4 seconds.", CBYTE (7, 1));
    text_put (2, 18, "keys seen only while it says off", CBYTE (3, 7));
    text_put (2, 19, "= the irq is starving the kernal.", CBYTE (3, 7));
    text_put (2, 21, "frames stops counting", CBYTE (3, 2));
    text_put (2, 22, "= the machine has stopped instead.", CBYTE (3, 2));

    irq_install ();

    for (;;) {
        frame ();
        ++ticks;

        /* Read through exactly the path the game reads through, so a pass
        ** here is a pass there. */
        if (kbhit ()) {
            last = (unsigned char)cgetc ();
            ++nkeys;
        }

        /* The A/B, run by the program rather than by the operator - the
        ** operator's keyboard is the thing on trial. */
        if (++phase >= SWAP_FRAMES) {
            phase = 0;
            irq_on ^= 1;
            if (irq_on) irq_install ();
            else        irq_remove ();
        }

        text_put (16, 4, irq_on ? "on " : "off",
                  irq_on ? CBYTE (7, 5) : CBYTE (7, 2));
        put_num (16, 6,  ticks,                     CBYTE (7, 1));
        put_num (16, 7,  (unsigned int)music_frames, CBYTE (7, 1));
        put_num (16, 9,  (unsigned int)NDX,          CBYTE (7, 7));
        put_num (16, 10, nkeys,                      CBYTE (7, 7));
        put_num (16, 11, (unsigned int)last,         CBYTE (7, 7));
    }
}
