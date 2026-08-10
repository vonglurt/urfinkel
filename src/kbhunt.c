/* ------------------------------------------------------------------------
 * kbhunt.c - find the matrix signature of the keys the menu actually uses.
 *
 * kbdiag.c established that the KERNAL's interrupt does not run under
 * cc65's plus4 runtime, so $EF is never filled and kbhit() can never see
 * anything.  kbtype.c then established, by hand, that TED's own keyboard
 * latch at $FF08 DOES change when keys are pressed.  So the route exists;
 * what is missing is the table that says which reading means which key.
 *
 * This program builds that table, one key at a time.  It asks for a key,
 * waits for the matrix to go quiet, then for something to go down, records
 * the WHOLE matrix as it stands, waits for release, and moves on.
 *
 * It hunts exactly the keys the cabinet menu needs - 1 2 3 4 5 and m -
 * because those are what stand between this machine and being playable.
 *
 * --- what the first version got wrong, kept here because it is the whole
 * --- lesson of this file ------------------------------------------------
 *
 * Version one recorded, per key, only "the first row that was not $FF, and
 * what that row read".  It produced a table in which 1, 2, 3 and 5 all read
 * 254 - four keys with one signature, which cannot decode anything - and
 * rows that jumped around between presses.  Two causes, both instructive:
 *
 *   1. BIT 7 IS NOT A KEYBOARD COLUMN.  Idle read 127 ($7F), not 255, so
 *      the "is anything down?" test was true forever.  Every capture fired
 *      on the settle timer rather than on a keypress, and what got recorded
 *      was whichever row noise reached first.  Bit 7 is masked back high
 *      here, and idle now reads idle.
 *   2. COLLAPSING EIGHT ROWS TO ONE THREW AWAY THE SIGNAL.  A key is a
 *      (row, column) intersection; one row's byte cannot identify it.  The
 *      full eight-byte signature is recorded and shown.
 *
 * A summary that loses the thing being measured is worse than no summary,
 * because it looks like data.
 *
 * A row reads 255 when nothing in it is held: the matrix is active low.
 *
 * Build and run: make kbhunt-run
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "board.h"
#include "text.h"

#define KBD_LATCH (*(volatile unsigned char*)0xFF08)
#define KBD_SEL   (*(volatile unsigned char*)0xFD30)

/* The menu's own keys, in the order the menu lists them. */
static const char TARGETS[] = { '1', '2', '3', '4', '5', 'm' };
#define NTARGETS  (sizeof (TARGETS) / sizeof (TARGETS[0]))

/* Frames a reading must persist before it is believed.  A key bounces, and
 * a table built from the first frame of a bounce is a table of noise. */
#define SETTLE    4

/* Bit 7 masked back high - see the note above.  Everything is active low,
** so $FF means "nothing held in this row". */
#define ROWVAL(v)  ((unsigned char)((v) | 0x80))

extern unsigned char scr_code (unsigned char c);

static unsigned char sig[NTARGETS][8];  /* the whole matrix, per key      */
static unsigned char got[NTARGETS];
static unsigned char live[8];

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

static void put_ch (unsigned char x, unsigned char y, char c,
                    unsigned char col)
{
    rowtab[y][x] = scr_code ((unsigned char)c);
    *(rowtab[y] + x - SCREEN_COLOR_D) = col;
}

/* Write the row mask to the scan latch, strobe the keyboard latch, read it
** back.  This is the protocol the first run showed row dependence under -
** '1' answered on row 7 where '2' answered on row 1 - which is the reason
** it is the one carried forward. */
static void scan_all (unsigned char* out)
{
    unsigned char i;

    for (i = 0; i < 8; ++i) {
        KBD_SEL   = (unsigned char)~(1 << i);
        KBD_LATCH = 0xFF;
        out[i] = ROWVAL (KBD_LATCH);
    }
}

static unsigned char any_down (const unsigned char* r)
{
    unsigned char i;

    for (i = 0; i < 8; ++i)
        if (r[i] != 0xFF) return 1;
    return 0;
}

/* One key's eight rows across one screen line: 8 x 3 digits, 4 columns
** apart, which is exactly what a forty column screen has room for. */
static void draw_sig (unsigned char y, const unsigned char* r,
                      unsigned char col)
{
    unsigned char i;

    for (i = 0; i < 8; ++i)
        put_num3 ((unsigned char)(6 + i * 4), y, r[i],
                  (unsigned char)(r[i] != 0xFF ? CBYTE (7, 5) : col));
}

static void draw_table (void)
{
    unsigned char i;

    for (i = 0; i < NTARGETS; ++i) {
        unsigned char y = (unsigned char)(8 + i);

        put_ch (2, y, TARGETS[i], got[i] ? CBYTE (7, 7) : CBYTE (3, 1));
        if (got[i]) draw_sig (y, sig[i], CBYTE (4, 1));
    }
}

void main (void)
{
    unsigned char idx = 0, held = 0, waiting_release = 1, i;

    board_init ();
    TED_BGCOLOR = CBYTE (0, 0);
    TED_BORDER  = CBYTE (2, 9);
    screen_fill (CH_SPACE, 0);

    text_centre (0, "ur finkel - key hunt", CBYTE (7, 7));
    text_put (2, 2, "press the key shown, then let go.", CBYTE (7, 1));
    text_put (2, 3, "255 = nothing held in that row.", CBYTE (5, 1));
    text_put (2, 4, "a row that is not 255 is the key.", CBYTE (5, 1));

    text_put (2, 6, "key r0  r1  r2  r3  r4  r5  r6  r7", CBYTE (5, 7));
    rows_fill (7, 7, CH_SOLID, CBYTE (2, 7));

    text_put (2, 16, "press:", CBYTE (7, 1));
    text_put (2, 18, "live", CBYTE (5, 1));

    for (;;) {
        frame ();

        scan_all (live);
        draw_sig (19, live, CBYTE (6, 1));
        draw_table ();

        if (idx >= NTARGETS) {
            text_put (2, 21, "all six captured.", CBYTE (7, 5));
            text_put (2, 22, "read off the row that is not 255", CBYTE (5, 1));
            text_put (2, 23, "and which bit of it went low.", CBYTE (5, 1));
            continue;
        }

        put_ch (9, 16, TARGETS[idx], CBYTE (7, 7));

        if (any_down (live)) {
            /* Do not accept a press until the matrix has been seen quiet
            ** at least once since the last one, so one long press cannot
            ** fill the whole table. */
            if (waiting_release) {
                text_put (2, 14, "let go first      ", CBYTE (4, 2));
                held = 0;
                continue;
            }
            text_put (2, 14, "reading...        ", CBYTE (7, 5));
            if (++held >= SETTLE) {
                for (i = 0; i < 8; ++i) sig[idx][i] = live[i];
                got[idx] = 1;
                ++idx;
                held = 0;
                waiting_release = 1;
            }
        } else {
            held = 0;
            waiting_release = 0;
            text_put (2, 14, "waiting for a key ", CBYTE (4, 1));
        }
    }
}
