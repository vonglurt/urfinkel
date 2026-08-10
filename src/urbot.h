/* ------------------------------------------------------------------------
 * urbot.h - the opponent, and the fact that it explains itself.
 *
 * URBOT does not search.  It holds a DOCTRINE - a standing intention about
 * how to play Ur, drawn once per match - and each turn it resolves that
 * doctrine against what the board actually offers.  Each turn costs one
 * scan of the legal moves into five opportunity registers and a handful of
 * comparisons, which is why it was affordable in interpreted BASIC and is
 * free here.
 *
 * The point of it is legibility: every branch narrates itself into the
 * chronicle, so a watcher can follow the standing intention, the state it
 * was applied to, the branch that fired and the move that resulted,
 * without reading the source.
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_URBOT_H
#define URFINKEL_URBOT_H

#define DOCTRINES       4
#define DOC_ONE_AT_A_TIME  0            /* drive a single runner          */
#define DOC_FILL_BOARD     1            /* crowd the corridor             */
#define DOC_RUN_FOR_IT     2            /* push the furthest piece        */
#define DOC_HUNT_AND_HOLD  3            /* captures first, then rosettes  */

extern unsigned char doctrine[2];
extern unsigned char doctrine_switched[2];

void urbot_new_match (void);
unsigned char urbot_choose (unsigned char player);   /* returns a piece 1-7 */

#endif
