/* ------------------------------------------------------------------------
 * kbtype.c - "type something", watched down both routes at once.
 *
 * kbdiag.c established, headlessly and without pressing anything, that the
 * KERNAL's interrupt does not run under cc65's plus4 runtime: the jiffy
 * clock at $A3-$A5 does not advance with our raster interrupt installed OR
 * removed, while our own handler counts frames normally.  The KERNAL scan
 * is what fills $EF, and kbhit() reads $EF, so if that is right then no key
 * can reach the game however hard it is pressed.
 *
 * This program is the human half of that finding.  It puts a prompt up and
 * shows, live, what arrives by each of two routes:
 *
 *   THE KERNAL ROUTE - $EF, kbhit(), cgetc().  What game.c's poll_key uses
 *   today.  If the finding is right this stays at zero no matter what is
 *   typed.
 *
 *   THE TED ROUTE - the keyboard latch at $FF08.  On this machine the
 *   keyboard is scanned by TED itself rather than by a CIA as on the C64,
 *   so the matrix can be read without the KERNAL being involved at all.
 *   If this moves while the other does not, the fix is to stop asking the
 *   KERNAL and read TED directly.
 *
 * ON THE TED ROUTE BEING SHOWN TWO WAYS.  The exact strobe protocol is the
 * one thing here I am not certain of, and a probe that quietly reports
 * nonsense is worse than no probe.  So both candidate protocols are run
 * side by side and shown as RAW MATRIX BITS rather than as decoded
 * characters - no keycode table is claimed:
 *
 *   via $FD30   write the row mask to the scan latch, strobe $FF08, read
 *   via $FF08   write the row mask straight to $FF08, read it back
 *
 * A row reads $FF (255) when nothing in it is held, because the matrix is
 * active low.  Hold a key and watch for a row that stops being 255.
 * Whichever column moves is the protocol this machine actually uses, and
 * that is a result worth having on its own.
 *
 * Nothing in this program is driven by the keyboard - the display updates
 * every frame regardless - because the keyboard is the thing on trial.
 *
 * Build and run: make kbtype-run
 * --------------------------------------------------------------------- */

#include <conio.h>
#include "plus4.h"
#include "board.h"
#include "text.h"
#include "kbd.h"

/* The KERNAL's count of characters waiting.  plus4.inc: KEY_COUNT := $EF.
** NOT $C6/198 and NOT the buffer at 631-640 - those are the C64's NDX and
** KEYD (c64.inc: KEY_COUNT := $C6).  On this machine the count is $EF and
** the buffer is at $0527. */
#define NDX     (*(volatile unsigned char*)0x00EF)

/* The KERNAL's jiffy clock (plus4.inc: TIME := $A3), all three bytes. */
#define TIME_0  (*(volatile unsigned char*)0x00A3)
#define TIME_1  (*(volatile unsigned char*)0x00A4)
#define TIME_2  (*(volatile unsigned char*)0x00A5)

/* TED's keyboard latch (plus4.inc: TED_KBD := $FF08) and the scan latch
** the row mask is conventionally written to.  Both volatile: the whole
** point of them is that they change underneath the program. */
#define KBD_LATCH (*(volatile unsigned char*)0xFF08)
#define KBD_SEL   (*(volatile unsigned char*)0xFD30)

extern volatile unsigned char music_frames;  /* the irq writes it */
void irq_install (void);
void irq_remove (void);

#define ECHO_MAX 28

static char          echo[ECHO_MAX + 1];
static unsigned char echo_n;
static unsigned int  keys_seen;
static unsigned char last_key;

static void frame (void)
{
    while (TED_RASTER_LO != 250) ;
    while (TED_RASTER_LO == 250) ;
}

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

static void put_num5 (unsigned char x, unsigned char y, unsigned int v,
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

/* Row mask is active low: bit n low selects row n. */
static unsigned char scan_via_sel (unsigned char row)
{
    KBD_SEL   = (unsigned char)~(1 << row);
    KBD_LATCH = 0xFF;                   /* strobe: latch the lines        */
    return KBD_LATCH;
}

static unsigned char scan_via_latch (unsigned char row)
{
    KBD_LATCH = (unsigned char)~(1 << row);
    return KBD_LATCH;
}

void main (void)
{
    unsigned char i, k;

    cursor (0);
    board_init ();
    kbd_init ();
    TED_BGCOLOR = CBYTE (0, 0);
    TED_BORDER  = CBYTE (2, 9);
    screen_fill (CH_SPACE, 0);

    text_centre (0, "ur finkel - type something", CBYTE (7, 7));
    text_put (2, 2, "go on, type. this display updates", CBYTE (7, 1));
    text_put (2, 3, "every frame whether you do or not.", CBYTE (7, 1));

    /* The kbd.c route is what the game reads through now, so it is what
    ** this instrument watches.  The KERNAL's $EF is kept as one number
    ** underneath it, because a reading of zero there while keys arrive
    ** above is the whole finding of docs/keyboard-report.md on screen. */
    text_put (2, 5, "kbd.c route - what the game uses", CBYTE (7, 5));
    text_put (4, 6,  "keys read", CBYTE (5, 1));
    text_put (4, 7,  "last code", CBYTE (5, 1));
    text_put (4, 8,  "echo", CBYTE (5, 1));
    text_put (4, 9,  "kernal ndx $ef (dead)", CBYTE (5, 1));

    text_put (2, 11, "the ted route - $ff08, raw bits", CBYTE (7, 5));
    text_put (4, 12, "plain latch", CBYTE (5, 1));
    text_put (4, 13, "rows via $fd30", CBYTE (5, 1));
    text_put (4, 15, "rows via $ff08", CBYTE (5, 1));

    text_put (2, 17, "kernal clock $a3 $a4 $a5", CBYTE (7, 5));
    text_put (2, 19, "our own irq frames", CBYTE (7, 5));

    text_put (2, 21, "255 = nothing held (active low).", CBYTE (3, 1));
    text_put (2, 22, "a row that stops being 255 while you", CBYTE (3, 1));
    text_put (2, 23, "hold a key, but no keys read, is decode.", CBYTE (3, 1));

    /* The game's own arrangement: our raster interrupt installed. */
    irq_install ();

    for (;;) {
        frame ();

        /* Exactly the path game.c takes: one scan a frame, then collect.
        ** wait_frames_live does the scan in the game; here the frame loop
        ** above is the same thing. */
        kbd_scan ();
        k = kbd_get ();
        if (k) {
            last_key = k;
            ++keys_seen;
            if (k >= 32 && echo_n < ECHO_MAX) {
                echo[echo_n++] = (char)k;
                echo[echo_n] = 0;
            }
        }

        put_num5 (20, 6, keys_seen, CBYTE (7, 7));
        put_num3 (20, 7, last_key, CBYTE (7, 7));
        if (echo_n) text_put (10, 8, echo, CBYTE (7, 1));
        put_num3 (28, 9, NDX,      CBYTE (7, 7));

        KBD_LATCH = 0xFF;
        put_num3 (20, 12, KBD_LATCH, CBYTE (7, 8));

        for (i = 0; i < 8; ++i) {
            k = scan_via_sel (i);
            put_num3 ((unsigned char)(4 + i * 4), 14, k,
                      (unsigned char)(k != 0xFF ? CBYTE (7, 5) : CBYTE (4, 8)));
        }
        for (i = 0; i < 8; ++i) {
            k = scan_via_latch (i);
            put_num3 ((unsigned char)(4 + i * 4), 16, k,
                      (unsigned char)(k != 0xFF ? CBYTE (7, 5) : CBYTE (4, 8)));
        }

        put_num3 (28, 17, TIME_0, CBYTE (7, 7));
        put_num3 (32, 17, TIME_1, CBYTE (7, 7));
        put_num3 (36, 17, TIME_2, CBYTE (7, 7));
        put_num5 (24, 19, (unsigned int)music_frames, CBYTE (7, 7));
    }
}
