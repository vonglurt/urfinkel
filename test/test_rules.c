/* ------------------------------------------------------------------------
 * test_rules.c - the frozen edition's rule table, run on the host.
 *
 * This is the same thirteen-row table that lives in urroyal.bas at line
 * 9600 and drives its unlisted mode 6.  Carrying it across verbatim is the
 * point: the two editions are held to one written-down expectation, and a
 * rule that changes in either has to change here too.
 *
 * Each row is
 *
 *   name, throw, expected moves, probe piece, its destination,
 *   piece to move, rosette flag, captures, board of A, board of B
 *
 * and the last three fields belong to the move executor, which has not
 * been ported yet.  They are carried anyway, unasserted and counted, so
 * that the day the executor lands the assertions are already written and
 * the deferred count drops to zero rather than the table needing to grow.
 *
 * Build and run with `make check` - it takes milliseconds, against about a
 * minute for the same checks under emulation.
 * --------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>
#include "rules.h"

struct check {
    const char*   name;
    unsigned char roll;
    unsigned char moves;        /* expected legal_count                   */
    unsigned char probe;        /* whose destination to assert, 0 = none  */
    unsigned char dest;         /* what it should be                      */
    unsigned char mover;        /* executor: piece to move, 0 = none      */
    unsigned char rosette;      /* executor: expected extra-throw flag    */
    unsigned char captures;     /* executor: expected captures            */
    unsigned char a[PIECES];    /* board of player A                      */
    unsigned char b[PIECES];    /* board of player B                      */
};

static const struct check table[] = {
 { "entry on the throw",           3, 7, 1,  3, 0, 0, 0,
   {0,0,0,0,0,0,0},               {0,0,0,0,0,0,0} },
 { "entry onto rosette 4",         4, 7, 1,  4, 1, 1, 0,
   {0,0,0,0,0,0,0},               {0,0,0,0,0,0,0} },
 { "entry blocked by own",         4, 1, 2,  0, 0, 0, 0,
   {4,0,0,0,0,0,0},               {0,0,0,0,0,0,0} },
 { "own piece blocks the way",     3, 6, 1,  0, 0, 0, 0,
   {6,9,0,0,0,0,0},               {0,0,0,0,0,0,0} },
 { "capture in the corridor",      3, 7, 1,  7, 1, 0, 1,
   {4,0,0,0,0,0,0},               {7,0,0,0,0,0,0} },
 { "the central rosette is safe",  3, 6, 1,  0, 0, 0, 0,
   {5,0,0,0,0,0,0},               {8,0,0,0,0,0,0} },
 { "private squares never meet",   2, 7, 1,  3, 1, 0, 0,
   {1,0,0,0,0,0,0},               {3,0,0,0,0,0,0} },
 { "blocked in, the turn is lost", 2, 0, 0,  0, 0, 0, 0,
   {2,4,6,8,10,12,14},            {0,0,0,0,0,0,0} },
 { "overshooting cannot bear off", 2, 0, 1,  0, 0, 0, 0,
   {14,15,15,15,15,15,15},        {0,0,0,0,0,0,0} },
 { "the exact count bears off",    1, 1, 1, 15, 1, 0, 0,
   {14,15,15,15,15,15,15},        {0,0,0,0,0,0,0} },
 { "rosette 8 throws again",       4, 1, 1,  8, 1, 1, 0,
   {4,0,0,0,0,0,0},               {0,0,0,0,0,0,0} },
 { "rosette 14 throws again",      2, 7, 1, 14, 1, 1, 0,
   {12,0,0,0,0,0,0},              {0,0,0,0,0,0,0} },
 { "no capture off the corridor",  1, 7, 1, 13, 1, 0, 0,
   {12,0,0,0,0,0,0},              {13,0,0,0,0,0,0} },
};

#define NCHECKS (sizeof (table) / sizeof (table[0]))

int main (void)
{
    unsigned int passed = 0, failed = 0, deferred = 0, i;
    unsigned char j;

    puts ("ur finkel - rule checks (host build of src/rules.c)\n");

    for (i = 0; i < NCHECKS; ++i) {
        const struct check* c = &table[i];
        int ok = 1;

        for (j = 0; j < PIECES; ++j) {
            piece[0][j + 1] = c->a[j];
            piece[1][j + 1] = c->b[j];
        }

        find_legal_moves (0, c->roll);

        if (legal_count != c->moves) {
            printf ("  FAIL %-30s moves %u, expected %u\n",
                    c->name, legal_count, c->moves);
            ok = 0;
        }
        if (c->probe && legal[c->probe] != c->dest) {
            printf ("  FAIL %-30s piece %u -> %u, expected %u\n",
                    c->name, c->probe, legal[c->probe], c->dest);
            ok = 0;
        }

        if (ok) { ++passed; printf ("  pass %s\n", c->name); }
        else      ++failed;

        if (c->mover) ++deferred;
    }

    printf ("\n%u of %zu generator checks passed\n", passed, NCHECKS);
    printf ("%u executor assertions deferred until move.c lands\n", deferred);

    if (failed) {
        printf ("\n*** %u check(s) failed ***\n", failed);
        return 1;
    }
    puts ("\nall generator checks passed");
    return 0;
}
