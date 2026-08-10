/* ------------------------------------------------------------------------
 * rules.c - the Finkel ruleset.
 *
 * A line-for-line port of the frozen edition's move generator and move
 * executor, kept that way on purpose: the acceptance test is that this
 * file reproduces the BASIC edition's thirteen rule checks and its
 * deterministic golden race exactly (28 throws for `four`, 6 for `zero`,
 * 7-0).  Cleverness here costs conformance, so the structure stays
 * recognisable and the speed comes from the types instead.
 *
 * The types are the whole story.  BASIC has one numeric type - a
 * five-byte float - so `P(cp,k)=np` is two array descriptors, two integer
 * to float conversions and a float compare.  Here it is a byte compare
 * against a pointer that was hoisted out of the loop.  That difference is
 * measured at 577x; see docs/lab-report.md section VI.
 * --------------------------------------------------------------------- */

#include "rules.h"

unsigned char piece[2][PIECES + 1];
unsigned char legal[PIECES + 1];
unsigned char legal_count;

unsigned char mv_from, mv_to, mv_captures, mv_rosette, mv_won;

const unsigned char rosette[SQ_HOME + 1] = {
    0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0
    /*          ^4          ^8                ^14        */
};

void find_legal_moves (unsigned char player, unsigned char roll)
{
    /* Hoisting the two board rows into pointers is the C equivalent of the
    ** BASIC edition's `ZA=SC+800` address hoisting, and buys the same
    ** thing: the inner loop stops recomputing an index it already knows. */
    unsigned char* mine   = piece[player];
    unsigned char* theirs = piece[player ^ 1];
    unsigned char  j, k, np, ok;

    legal_count = 0;
    for (j = 1; j <= PIECES; ++j) {
        legal[j] = 0;

        np = (unsigned char)(mine[j] + roll);
        if (mine[j] == SQ_HOME || np > SQ_HOME) continue;   /* no overshoot */

        ok = 1;
        for (k = 1; k <= PIECES; ++k) {
            if (np < SQ_HOME && mine[k] == np) ok = 0;      /* own piece    */
            if (np == SQ_SAFE_ROSETTE && theirs[k] == SQ_SAFE_ROSETTE)
                ok = 0;                                     /* safe rosette */
        }

        if (ok) {
            legal[j] = np;
            ++legal_count;
        }
    }
}

void execute_move (unsigned char player, unsigned char pc)
{
    unsigned char* mine   = piece[player];
    unsigned char* theirs = piece[player ^ 1];
    unsigned char  k, np;

    mv_from     = mine[pc];
    mv_to       = np = legal[pc];
    mv_captures = 0;
    mv_rosette  = 0;
    mv_won      = 0;

    /* Capture applies only in the shared corridor.  Outside it the two
    ** players' path squares carry the same numbers but are different
    ** cells, so pieces pass each other without ever meeting. */
    if (np >= SHARED_FIRST && np <= SHARED_LAST) {
        for (k = 1; k <= PIECES; ++k) {
            if (theirs[k] == np) {
                theirs[k] = SQ_POOL;
                ++mv_captures;
            }
        }
    }

    mine[pc] = np;

    if (np == SQ_HOME) {
        if (count_at_home (player) == PIECES) mv_won = 1;
    } else if (rosette[np]) {
        mv_rosette = 1;                 /* a rosette grants another throw */
    }
}

unsigned char count_at_home (unsigned char player)
{
    unsigned char* mine = piece[player];
    unsigned char  j, n = 0;

    for (j = 1; j <= PIECES; ++j)
        if (mine[j] == SQ_HOME) ++n;
    return n;
}

unsigned char count_in_pool (unsigned char player)
{
    unsigned char* mine = piece[player];
    unsigned char  j, n = 0;

    for (j = 1; j <= PIECES; ++j)
        if (mine[j] == SQ_POOL) ++n;
    return n;
}

unsigned char count_afield (unsigned char player)
{
    unsigned char* mine = piece[player];
    unsigned char  j, n = 0;

    for (j = 1; j <= PIECES; ++j)
        if (mine[j] > SQ_POOL && mine[j] < SQ_HOME) ++n;
    return n;
}

void reset_board (void)
{
    unsigned char q, j;

    for (q = 0; q < 2; ++q)
        for (j = 0; j <= PIECES; ++j)
            piece[q][j] = SQ_POOL;
}
