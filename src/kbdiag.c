/* ------------------------------------------------------------------------
 * kbdiag.c - is the KERNAL's interrupt still running?  Answered headlessly.
 *
 * kbtest.c is the BENCH instrument: it wants a human holding a key down.
 * This is its host-testable half, and it exists because of a gap in the
 * test procedure that backlog.md section C states plainly - a key reaches
 * the program in two stages,
 *
 *   1. the KERNAL's interrupt scans the matrix and bumps the count at $EF
 *   2. kbhit() reads $EF and cgetc() takes the character
 *
 * and stage 1 cannot be tested from the host by pressing keys, because
 * VICE's -keybuf writes $EF directly and so jumps over exactly the stage
 * under suspicion.  Every headless keyboard test this project could write
 * that way would pass whether stage 1 worked or not.
 *
 * THE WAY ROUND IT.  Do not press a key.  The KERNAL's interrupt handler
 * does two things on every tick: it scans the keyboard AND it advances the
 * jiffy clock at TIME ($A3-$A5).  The clock needs no keystroke, no buffer
 * and no -keybuf to observe.  So:
 *
 *     the jiffy clock is advancing  ->  the KERNAL handler is running
 *                                       ->  the keyboard is being scanned
 *     the jiffy clock is frozen     ->  it is not, and no key can ever
 *                                       arrive however hard it is pressed
 *
 * That turns "can I type" into something a screenshot can answer.
 *
 * WHY IT IS AN A/B.  UR FINKEL takes over the vector at $0314 for the
 * music and passes non-raster interrupts on to whatever was there before
 * (irq.s, `jmp (old_irq)`).  If that hand-off is broken the KERNAL never
 * runs.  So the clock is measured over two windows - one with our raster
 * interrupt REMOVED, one with it INSTALLED - and the pair of numbers says
 * which of three quite different things is wrong:
 *
 *     off alive, on alive   the interrupt path is fine; a dead keyboard
 *                           is somewhere above this, in the game
 *     off alive, on DEAD    our raster interrupt is starving the KERNAL.
 *                           The fault is in irq.s, in how handler chains
 *     off DEAD,  on DEAD    the KERNAL interrupt does not run under cc65
 *                           at all, with or without us - which would mean
 *                           the keyboard has never worked in any build and
 *                           irq.s is not involved
 *
 * The third outcome is the one no test in this project could previously
 * have distinguished from the second, and they call for opposite fixes.
 *
 * Build: make kbdiag.  Headless verdict: make kbdiag-shot.
 * --------------------------------------------------------------------- */

#include <conio.h>
#include "plus4.h"
#include "board.h"
#include "text.h"

/* The KERNAL's jiffy clock (plus4.inc: TIME := $A3), three bytes.
**
** ALL THREE are watched, and that is not thoroughness for its own sake.
** The whole verdict below rests on "this counter is not moving", and a
** counter can also appear not to move because the wrong end of it is
** being read - the byte that ticks 50 times a second and the byte that
** ticks once every five minutes look identical over a two second window.
** Reading all three removes the one way this program could lie. */
#define TIME_0  (*(volatile unsigned char*)0x00A3)
#define TIME_1  (*(volatile unsigned char*)0x00A4)
#define TIME_2  (*(volatile unsigned char*)0x00A5)

/* The KERNAL's count of characters waiting, which is what kbhit() reads
** (plus4.inc: KEY_COUNT := $EF). */
#define NDX     (*(volatile unsigned char*)0x00EF)

/* TED's keyboard latch and the row-select latch (plus4.inc: TED_KBD). */
#define KBD_LATCH (*(volatile unsigned char*)0xFF08)
#define KBD_SEL   (*(volatile unsigned char*)0xFD30)

extern volatile unsigned char music_frames;  /* bumped by irq.s */
void irq_install (void);
void irq_remove (void);

/* Two seconds a window at 50 Hz.  Long enough that a clock ticking at any
** plausible rate is unmistakable against one that is not ticking at all. */
#define WINDOW  100

static unsigned int keys_seen;
static unsigned char last_key;

static void frame (void)
{
    while (TED_RASTER_LO != 250) ;
    while (TED_RASTER_LO == 250) ;
}

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

/* How far each byte of the KERNAL's clock moves across WINDOW raster
** frames.  The wait is on the raster and not on the clock, deliberately:
** a frozen clock must end the window, not hang inside it. */
static unsigned char d0, d1, d2;

static void kernal_ticks (void)
{
    unsigned char a = TIME_0, b = TIME_1, c = TIME_2;
    unsigned char i;

    for (i = 0; i < WINDOW; ++i) {
        frame ();
        /* Read through the same path the game reads through, so a key that
        ** arrives here would have arrived there. */
        if (kbhit ()) {
            last_key = (unsigned char)cgetc ();
            ++keys_seen;
        }
    }
    d0 = (unsigned char)(TIME_0 - a);
    d1 = (unsigned char)(TIME_1 - b);
    d2 = (unsigned char)(TIME_2 - c);
}

/* Moving at all, at whichever end. */
static unsigned char moved (void)
{
    return (unsigned char)(d0 || d1 || d2);
}

/* Three digits, which is all a byte delta needs, and all the room there is
** to show three of them on a forty column screen. */
static void put_num3 (unsigned char x, unsigned char y, unsigned char v,
                      unsigned char col)
{
    unsigned char d = 100, i = 0;

    while (d) {
        rowtab[y][x + i] = (unsigned char)('0' + (unsigned char)(v / d));
        *(rowtab[y] + x + i - SCREEN_COLOR_D) = col;
        v = (unsigned char)(v % d);
        d = (unsigned char)(d / 10);
        ++i;
    }
}

/* The three bytes of TIME, in the order they sit in memory: $A3 $A4 $A5. */
static void show3 (unsigned char y, unsigned char col)
{
    put_num3 (28, y, d0, col);
    put_num3 (32, y, d1, col);
    put_num3 (36, y, d2, col);
}

void main (void)
{
    unsigned char off_alive, on_alive;

    cursor (0);
    board_init ();
    TED_BGCOLOR = CBYTE (0, 0);
    TED_BORDER  = CBYTE (2, 9);
    screen_fill (CH_SPACE, 0);

    text_centre (1, "ur finkel - kernal irq diagnosis", CBYTE (7, 7));
    text_put (2, 3, "does the kernal interrupt still run?", CBYTE (5, 1));
    text_put (2, 4, "it scans the keyboard and advances", CBYTE (5, 1));
    text_put (2, 5, "the jiffy clock - so the clock", CBYTE (5, 1));
    text_put (2, 6, "answers without pressing anything.", CBYTE (5, 1));

    text_put (2, 8, "kernal clock $a3 $a4 $a5 moved by", CBYTE (5, 1));

    /* Window A: our raster interrupt NOT installed.  This is the control -
    ** if the clock is already frozen here, nothing irq.s does is to blame. */
    text_put (2, 9, "with our raster irq off:", CBYTE (7, 1));
    irq_remove ();
    kernal_ticks ();
    show3 (9, CBYTE (7, 7));
    off_alive = moved ();

    /* Window B: the game's own arrangement. */
    text_put (2, 10, "with our raster irq on:", CBYTE (7, 1));
    irq_install ();
    kernal_ticks ();
    show3 (10, CBYTE (7, 7));
    on_alive = moved ();
    irq_remove ();

    /* Which of two quite different things stopped the KERNAL.  If TED's
    ** interrupt mask has the timer bits clear, something masked them and
    ** they can be set again; if they are set and the handler still does
    ** not run, there is no handler there to run - the ROM is banked out -
    ** and re-enabling anything would only fire into RAM. */
    text_put (2, 11, "ted imr $ff0a:", CBYTE (5, 1));
    put_num3 (17, 11, TED_IMR, CBYTE (7, 8));
    text_put (23, 11, "irr $ff09:", CBYTE (5, 1));
    put_num3 (34, 11, TED_IRR, CBYTE (7, 8));

    text_put (2, 12, "frames our own irq counted:", CBYTE (7, 1));
    put_num (33, 12, (unsigned int)music_frames, CBYTE (7, 7));
    text_put (2, 13, "ndx ($ef) now:", CBYTE (7, 1));
    put_num (33, 13, (unsigned int)NDX, CBYTE (7, 7));
    text_put (2, 14, "keys read through kbhit/cgetc:", CBYTE (7, 1));
    put_num (33, 14, keys_seen, CBYTE (7, 7));
    text_put (2, 15, "last key code:", CBYTE (7, 1));
    put_num (33, 15, (unsigned int)last_key, CBYTE (7, 7));

    /* THE IDLE MATRIX.  Nothing is held in a headless run, so every row
    ** must read 255; any row that does not has a line stuck low.  This
    ** matters because kbd_init calibrates against idle and permanently
    ** ignores whatever is low then - a stuck bit 7 would silently kill the
    ** whole of column 7, which carries SHIFT, X, V, N and the comma, and
    ** N is the "no" of the colour picker's y/n prompt. */
    text_put (2, 22, "idle matrix - all 255 = nothing stuck", CBYTE (5, 1));
    {
        unsigned char i, v, bad = 0;
        for (i = 0; i < 8; ++i) {
            KBD_SEL   = (unsigned char)~(1 << i);
            KBD_LATCH = 0xFF;
            v = KBD_LATCH;
            if (v != 0xFF) bad = 1;
            put_num3 ((unsigned char)(2 + i * 4), 23, v,
                      (unsigned char)(v != 0xFF ? CBYTE (7, 2) : CBYTE (5, 5)));
        }
        text_put (2, 24, bad ? "a line is stuck - column 7 at risk"
                             : "clean - every key is reachable",
                  bad ? CBYTE (7, 2) : CBYTE (7, 5));
    }

    /* The verdict, spelled out, because the whole point of this program is
    ** that a screenshot of it decides what to change next. */
    if (off_alive && on_alive) {
        text_put (2, 18, "VERDICT: kernal irq alive both ways.", CBYTE (7, 5));
        text_put (2, 19, "the keyboard scan is running.  a key", CBYTE (7, 5));
        text_put (2, 20, "that does not arrive is being lost", CBYTE (7, 5));
        text_put (2, 21, "above this, inside the game.", CBYTE (7, 5));
    } else if (off_alive) {
        text_put (2, 18, "VERDICT: our raster irq stops the", CBYTE (7, 2));
        text_put (2, 19, "kernal.  the clock runs with it off", CBYTE (7, 2));
        text_put (2, 20, "and stops with it on.  the fault is", CBYTE (7, 2));
        text_put (2, 21, "in irq.s - handler chaining old_irq.", CBYTE (7, 2));
    } else if (on_alive) {
        text_put (2, 18, "VERDICT: inverted - the clock only", CBYTE (7, 8));
        text_put (2, 19, "runs with our irq installed.  read", CBYTE (7, 8));
        text_put (2, 20, "the numbers, not this program.", CBYTE (7, 8));
    } else {
        text_put (2, 18, "VERDICT: the kernal irq never runs,", CBYTE (7, 2));
        text_put (2, 19, "with us or without us.  the keyboard", CBYTE (7, 2));
        text_put (2, 20, "cannot work in any build, and irq.s", CBYTE (7, 2));
        text_put (2, 21, "is not what is wrong.", CBYTE (7, 2));
    }

    for (;;) ;                          /* park for -exitscreenshot */
}
