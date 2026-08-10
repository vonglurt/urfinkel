/* ------------------------------------------------------------------------
 * bench.c - the migration's headline measurement.
 *
 * Times the three renderer primitives that the BASIC edition's own
 * benchmark times, on the same machine, against the same PAL jiffy clock
 * that BASIC's TI counts.  Each is repeated enough times to be several
 * jiffies long, because one jiffy (20 ms) is coarser than any single
 * compiled draw.
 *
 * Run `make bench` in this directory to produce both halves of the
 * comparison; the numbers land in docs/lab-report.md section V.
 * --------------------------------------------------------------------- */

#include <conio.h>
#include <time.h>
#include "plus4.h"
#include "rules.h"
#include "board.h"

#define REPS_BOARD      100U
#define REPS_BUTTON     2000U
#define REPS_TOKENS     1000U
#define REPS_MOVES      2000U

/* Microseconds per operation, from a jiffy count and a repetition count.
** 1 PAL jiffy = 20000 us.  Done in unsigned long so the intermediate does
** not overflow, and without floating point so nothing links in fp code. */
static unsigned long per_op_us (unsigned long jiffies, unsigned int reps)
{
    return (jiffies * 20000UL) / reps;
}

static void report (const char* what, unsigned long jiffies, unsigned int reps)
{
    cprintf ("%-14s%5lu jif %6lu us each\r\n",
             what, jiffies, per_op_us (jiffies, reps));
}

void main (void)
{
    clock_t       t0;
    unsigned long jb, jk, jt, jm;
    unsigned int  i;

    board_init ();

    /* --- the full board draw: BASIC 1000 ------------------------------ */
    t0 = clock ();
    for (i = 0; i < REPS_BOARD; ++i) board_draw ();
    jb = (unsigned long)(clock () - t0);

    /* --- one 4x4 shaded button: BASIC 7200 ---------------------------- */
    t0 = clock ();
    for (i = 0; i < REPS_BUTTON; ++i) paint_button (0, 0, 87, 0, 0, 0);
    jk = (unsigned long)(clock () - t0);

    /* --- both token rows: BASIC 7650 ---------------------------------- */
    t0 = clock ();
    for (i = 0; i < REPS_TOKENS; ++i) paint_tokens ();
    jt = (unsigned long)(clock () - t0);

    /* --- the legal-move generator: BASIC 3500 -------------------------
    ** Same board as the BASIC benchmark: every piece still in the pool
    ** and a throw of 2, so all seven candidates run the full inner scan
    ** and neither version gets an early exit the other does not. */
    t0 = clock ();
    for (i = 0; i < REPS_MOVES; ++i) find_legal_moves (0, 2);
    jm = (unsigned long)(clock () - t0);

    /* board_draw leaves the TED background at true black, which is what
    ** the playfield wants and what the report screen has to allow for:
    ** conio's default text colour disappears into it. */
    textcolor (COLOR_WHITE);
    clrscr ();
    cprintf ("ur finkel - compiled renderer\r\n");
    cprintf ("cc65 %s, pal jiffies (50/s)\r\n\r\n", "2.18");

    cprintf ("op            total      per call\r\n");
    cprintf ("----------------------------------\r\n");
    report ("board  x100",  jb, REPS_BOARD);
    report ("button x2000", jk, REPS_BUTTON);
    report ("tokens x1000", jt, REPS_TOKENS);
    report ("moves  x2000", jm, REPS_MOVES);

    cprintf ("\r\nlegal moves found: %u (expect 7)\r\n", legal_count);
    cprintf ("compare the basic edition: make bench\r\n");

    for (;;) ;                          /* park for -exitscreenshot */
}
