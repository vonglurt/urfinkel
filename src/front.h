/* ------------------------------------------------------------------------
 * front.h - the cabinet the game is played inside.
 *
 * The frozen edition's theatre front (BASIC 8400-8495), lamp chase (8500),
 * curtain (8700) and gold trophy (6200-6285), compiled.  It is a separate
 * module from game.c because none of it is the controller: the controller
 * decides what happens next, and this decides what the machine looks like
 * while it happens.
 *
 * The proscenium:
 *
 *      row  0    lamp band
 *      rows 1,3  woven frieze, gold-brown-yellow
 *      row  2    the banner, zigzag flanks
 *      row  4    lamp band
 *      row  5    pelmet
 *      row  6    valance, a fringe with a gap every fourth column
 *      rows 7-23 THE STAGE, with the curtains running in it
 *      row  23   the stage floor, and the engraved credit
 *      row  24   lamp band
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_FRONT_H
#define URFINKEL_FRONT_H

#define STAGE_TOP       7
#define STAGE_BOT       23

/* Where a curtain rests when it is open: six columns each side, which is
** the width of the wings the BASIC edition drew.  Closed, the two halves
** meet in the middle of a 40-column screen. */
#define CURTAIN_OPEN    6
#define CURTAIN_SHUT    20

/* Half open, which is where a reveal can start showing its contents.  The
** curtain's edge counts DOWN from CURTAIN_SHUT to CURTAIN_OPEN as it
** opens, so "edge <= CURTAIN_HALF" reads as "at least half way". */
#define CURTAIN_HALF    ((CURTAIN_SHUT + CURTAIN_OPEN) / 2)

/* The interior the menu is drawn into while the curtains are open. */
#define STAGE_LEFT      CURTAIN_OPEN
#define STAGE_RIGHT     (39 - CURTAIN_OPEN)

/* The whole front, with the curtains wherever `edge` puts them:
** CURTAIN_OPEN for a bare stage, CURTAIN_SHUT for a covered one. */
void theatre_draw (unsigned char edge);

/* One frame of the marquee chase - colour RAM only, 60 bytes. */
void lamps_frame (unsigned char phase);

/* The curtains, at a watchable pace.  Opening plays the fanfare and takes
** as long as the fanfare does; closing is quieter and a little quicker.
** curtain_shut is the instant version, for dressing the stage behind. */
void curtain_shut (void);
void curtain_close (void);

/* `dress` repaints whatever belongs on the stage; it is called once per
** step, before the remaining curtain goes back over the top, because
** there is nowhere on this machine to keep a copy of what the curtain is
** hiding.  Pass 0 to open onto a bare stage.
**
** It is handed the curtain's current edge so that it can reveal its
** contents in stages rather than all at once - the stage knows how open it
** is, and only the caller knows what should be visible when.  Compare
** against CURTAIN_HALF for "not until we are half way". */
void curtain_open (void (*dress) (unsigned char edge));

/* The gold cup, painted over the casting floor, with the winner's name
** engraved across its bowl. */
void trophy_draw (const char* winner);

#endif
