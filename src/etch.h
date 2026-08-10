/* ------------------------------------------------------------------------
 * etch.h - screen effects, after terminaltexteffects.
 *
 * Three of them: a laser that etches a word, fire that burns one in, and
 * fireworks that burst over one.  All three are taken from
 * src/terminaltexteffects-main as MECHANISMS rather than as code - see
 * each one's note for what carried over and what could not.
 *
 * All three can be had at two sizes now.  The word is either ordinary
 * text on one row or BLOCK LETTERS across the whole screen (etch_word),
 * and the fire and the fireworks are sized to the second: see `the apron`
 * below for what that means and why it is affordable.
 *
 * Ported in spirit, not in code, from the `laseretch` effect in
 * terminaltexteffects (src/terminaltexteffects-main).  That is a Python
 * screensaver for a truecolour terminal; this is a 40x25 text screen with
 * sixteen hues and eight luminances and one megahertz.  What carries over
 * is the MECHANISM, because the mechanism is what makes it read as a laser:
 *
 *   1. a BEAM - a diagonal line rising up and to the right from the point
 *      being cut, colour-cycled so it flickers;
 *   2. characters etched ONE GROUP AT A TIME along a pattern, not all at
 *      once, so the word is cut rather than printed;
 *   3. each character spawns WHITE HOT and then COOLS down a gradient to
 *      its final colour, and goes on cooling while the beam moves on;
 *   4. SPARKS thrown off the cutting point, falling away under it.
 *
 * Point 3 is the one this machine is unexpectedly good at.  The original
 * cools through an arbitrary RGB gradient; TED has eight luminances of
 * every hue, which is a cooling ramp already - white, through the hue's own
 * bright end, down to where it should rest.  The effect was waiting for it.
 *
 * WHAT IS DELIBERATELY DIFFERENT.  The original is paced to be watched
 * idly: `etch_speed` characters every `etch_delay` frames, tuned for a
 * screensaver.  This is a MARQUEE inside a game, and the game has to get
 * on with itself - so the caller states how long the whole thing may take
 * and the effect fits itself into that, cutting more characters per frame
 * for a longer word rather than running longer.  A capture gets four
 * seconds and a win gets nearer twelve, whatever the word is.
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_ETCH_H
#define URFINKEL_ETCH_H

/* --- the apron --------------------------------------------------------
**
** THE WHOLE BOTTOM OF THE SCREEN, not one row of it.
**
** The two moments that stop the game - a capture and a win - used to be a
** word cut into a single row in the middle of the casting floor: eight
** cells out of the eleven rows below the board.  A capture is the most
** violent thing that happens in Ur and it was being shown smaller than a
** throw of the dice, which happens every turn.
**
** So the big effects take the apron - rows 14 to 24, everything under the
** board: the lots' floor AND the chronicle.  It is called the apron and
** not the stage because `stage` is already the theatre front's, meaning
** the part of the screen between the curtains; this is the strip in front
** of the board, which is what an apron is.
**
** THE LOG IS WHAT MAKES IT AFFORDABLE.  The chronicle's four lines live in
** a buffer that can repaint itself (chronicle_redraw), so covering them
** costs nothing that cannot be put back - and the floor is empty between
** throws in any case.  apron_clear puts both back when the effect is over.
**
** APRON_BOT is 24 and not 23: the last row is the play-again prompt's, and
** between prompts it is empty. */
#define APRON_TOP       14
#define APRON_BOT       24

/* Put the casting floor and the chronicle back after an effect has had
** the apron.  Every entry point below leaves its own marks standing, so
** the caller decides how long the picture holds before this runs. */
void apron_clear (void);

/* Etch `s` centred on row `y`, finishing in about `frames` frames.
**
** The whole word is cut, cooled and left standing before this returns, so
** a caller that asks for 100 frames has spent two seconds when it comes
** back.  The cells it touched are its own; the caller repaints. */
void etch_text (const char* s, unsigned char y, unsigned char frames);

/* The same cut, in BLOCK LETTERS - four columns by five rows each, one
** character cell per lit pixel, at a five column pitch.  Eight letters is
** thirty-nine columns, so a word of up to eight characters spans the
** screen; `y` is its top row and it is five rows deep.
**
** Eight is not an arbitrary limit.  It is the length of a player's name,
** which is what this is for: the winner's name across the whole stage
** rather than eight cells in the middle of it.  A shorter word is
** centred, not stretched - the pitch is fixed so that every word is cut
** at the same size, and a name is recognisable by its shape.
**
** Letters and digits only; anything else takes its slot and stays blank. */
void etch_word (const char* s, unsigned char y, unsigned char frames);

/* The two letters, drawn large, cut the same way.  Used once, on the way
** in, while the trumpets play. */
void etch_big_ur (unsigned char frames);

/* The whole title, as a sequence: the two large letters first, then the
** word cut underneath them.
**
** It is two cuts and not one because nine letters at six columns each is
** fifty-four columns and the screen has forty.  Splitting it is not a
** compromise - a title that assembles in two moves reads better than one
** that appears, and the large UR is the part worth making large. */
void etch_title (unsigned char frames);

/* --- burn -------------------------------------------------------------
**
** From the same library's `burn`, and it is a genuinely different animal
** from the laser rather than the same idea in another colour.  Two things
** make it so, and both are kept:
**
**   1. IGNITION SPREADS TO NEIGHBOURS.  The original walks a spanning tree
**      over the characters, so fire reaches a cell because the cell beside
**      it is already alight.  A word here is one row, so the tree is a
**      line - but it is lit from a point and spreads BOTH WAYS, which is
**      what fire does and what reading order is not.
**   2. THE GLYPH ITSELF CHANGES.  The original morphs each cell through
**      ' . ▖ ▙ █ ▜ ▀ ▝ . while walking a fire gradient - so a burning cell
**      is not the final character in a hot colour, it is a lump of flame
**      that only becomes the character once it has cooled.  That is the
**      whole difference between burning a word in and colouring it in.
**
** The laser cuts a word out of nothing; this sets an existing word alight
** and lets it settle back.  A capture is the second: the piece was there a
** moment ago.
**
** Smoke is dropped.  The original drifts it upward from spent cells, and
** on the casting floor there is nowhere for it to go that is not the
** board.
**
** WHAT THE STAGE ADDED, and it is the difference between a word being
** coloured red and something being on fire:
**
**   3. THERE IS A FIRE UNDERNEATH IT.  A bed of flame along the bottom
**      six rows, one height per column, rippling sideways and breathing
**      up and down on an envelope - building, blazing, then dying back to
**      nothing.  The word sits directly above the tips.
**   4. IGNITION CLIMBS.  Point 1's spreading tree is now two-dimensional
**      and the fire comes from BELOW: a cell's ignition is delayed by how
**      far it is above the flames, so the bottom row of the letters
**      catches first and the fire walks up them.  A word alight from the
**      bottom is being burned; a word alight from the middle is being
**      decorated.
**
** The word is in block letters (see etch_word) and it stands, orange and
** spent, when this returns.  It owns rows APRON_TOP..APRON_BOT. */
void burn_apron (const char* s, unsigned char frames);

/* --- fireworks --------------------------------------------------------
**
** From the same library's `fireworks`, and given the same treatment: the
** shape of it, not the code.  There, characters launch from the bottom to
** an apex on an out_expo ease, burst onto a circle around it on out_circ,
** bloom outward along a bezier, and finally fall into their place in the
** text.  Four movements.
**
** Here the first three are kept and the fourth is not, because the word is
** already on screen - the winner's name has been cut by the laser before
** this runs, and fireworks that assembled it again would be saying the
** same thing twice.  So these burst OVER it: a celebration around a name
** rather than a way of delivering one.
**
** SEVERAL SHELLS ARE IN THE AIR AT ONCE, which is the whole difference
** between a display and a demonstration.  The first version fired four,
** strictly one after another, each waiting for the last to go out: thirty
** two frames of one small ring, four times.  Three are live now and a new
** one launches every eleven frames, so a burst is always fading while the
** next is climbing, and the sky is never empty.
**
** They are also twice the size - twelve particles on a twenty-four column
** ring rather than eight on a ten - and each burst FLASHES THE BORDER in
** its own hue for two frames, which is the one way a forty column screen
** can light the room up.
**
** Runs for `frames` and leaves the screen, and the border, exactly as it
** found them: every cell is borrowed and given back. */
void fireworks (unsigned char frames);

#endif
