/* ------------------------------------------------------------------------
 * dice.h - four tetrahedral lots, and the randomness behind them.
 *
 * Ur is not played with cubes.  The dice are four tetrahedral lots, each
 * with two of its four corners tipped in white; a throw lands each lot
 * tip-up or blank-up, and the score is the number of tips showing.  So a
 * throw is four independent coin flips summed, which is the correct
 * binomial 0-4 - 0 and 4 at 1/16 each, 2 at 6/16 - and not a uniform
 * 0-4.  The BASIC edition got this wrong in its opening ceremony once and
 * the fix is recorded in its backlog as 14.1.
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_DICE_H
#define URFINKEL_DICE_H

#define LOTS            4
#define LOT_HOME_ROW    17
/* THE FLOOR MOVED DOWN TWO AND GAVE ONE BACK.
**
** The board grew from twelve rows to fourteen when the black separators
** went in between the bands, and on a twenty-five row screen that has to
** come from somewhere. It comes from here: the floor starts two rows
** lower and ends one row lower, so it is eight rows instead of nine.
**
** FLOOR_BOT is 21, which is ALSO the chronicle's top line. That overlap is
** deliberate - the lots roll across the top line of the log - and it is
** the only reason the two extra board rows fit at all. floor_wipe clears
** it, so the oldest chronicle line goes when the lots are cast; the log is
** four lines and scrolls, so what it costs is the line already on its way
** out. */
#define FLOOR_TOP       14              /* the casting floor, rows 14-21  */
#define FLOOR_BOT       21

/* --- randomness ------------------------------------------------------- */
/* A compiled program has no RND to seed, so it brings its own: a 16-bit
** xorshift, seeded from the machine's own timing.  The BASIC edition
** accumulated entropy in the menu's attract loop; here the raster
** position at the moment a key is pressed is a finer-grained version of
** the same idea - it changes 312 times a frame rather than once. */
void rnd_seed (unsigned int s);
void rnd_stir (void);                   /* fold in the current raster     */
unsigned char rnd_byte (void);
unsigned char rnd_below (unsigned char n);

/* --- the lots --------------------------------------------------------- */

extern unsigned char lot_tip[LOTS];     /* 1 = a tipped corner is showing */
extern unsigned char thrower_colour;    /* the hue the tips are painted in */

unsigned char throw_lots (void);        /* the tumble, animated; 0-4      */
unsigned char throw_quiet (void);       /* the same throw, no animation   */
void lots_show (unsigned char tips);    /* settled lots for a given count */
void floor_wipe (void);                 /* clear the casting floor        */

#endif
