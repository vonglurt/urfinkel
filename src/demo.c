/* ------------------------------------------------------------------------
 * demo.c - the renderer conformance proof.
 *
 * Draws the opening board exactly once and parks, so that a screenshot of
 * this program can be compared against a screenshot of the frozen BASIC
 * edition drawing the same position.  Same colours, same geometry, same
 * screen codes: if the two images differ, the port is wrong, and the
 * difference is visible rather than argued about.
 *
 * `make conform` builds both halves and diffs them.
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "rules.h"
#include "board.h"

void main (void)
{
    unsigned char q, j;

    board_init ();

    /* The opening position: every piece still in the pool, which is what
    ** the BASIC edition's board draw produces at the start of a match. */
    for (q = 0; q < 2; ++q)
        for (j = 0; j <= PIECES; ++j)
            piece[q][j] = SQ_POOL;

    board_draw ();

    for (;;) ;
}
