/* ------------------------------------------------------------------------
 * dice.c - the four lots, drawn as what they are.
 *
 * Each lot is a pyramid rather than a square: a bone-white base block, two
 * PETSCII diagonals rising to an apex cell that holds either a filled disc
 * (tipped, in the thrower's hue) or a hollow ring (blank).
 *
 *        (*)         apex: filled = 1, hollow = 0
 *        / \
 *       #####        base
 *
 * The tumble carries absolute floor coordinates rather than offsets, so
 * settling is just "put the coordinates back to the home slot" and the
 * scatter is free to use the whole floor.  No two lots may overlap: a
 * scattered position is rejection-sampled against all four current
 * positions, capped so a pathological draw cannot hang the throw.
 *
 * The BASIC edition ran one to three tumble frames because each frame cost
 * a fortune in POKEs.  Here a frame is under a millisecond, so the tumble
 * runs long enough to read as dice being thrown rather than as a flicker.
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "rules.h"
#include "board.h"
#include "text.h"
#include "dice.h"
#include "music.h"
#include "dbg.h"

/* The pace of a throw, in frames.  Kept here as named constants rather
** than as numbers inside the loop, because they are the only thing that
** decides whether a throw reads as dice being cast or as a flicker, and
** they should be adjustable without reading the algorithm. */
#define TUMBLE_PACE     2               /* between scatter frames         */
#define SETTLE_PACE     4               /* after a lot comes to rest      */

unsigned char lot_tip[LOTS];

static unsigned char lx[LOTS], ly[LOTS];        /* where a lot is drawn   */
static unsigned char ex[LOTS], ey[LOTS];        /* where it was drawn     */
static unsigned char settled[LOTS];

/* The hue a tipped corner is painted in - the thrower's own colour.
** The controller sets it at the top of every turn. */
unsigned char thrower_colour = 0x71;

/* --- randomness ------------------------------------------------------- */

static unsigned int rnd_state = 0xACE1U;

void rnd_seed (unsigned int s)
{
    rnd_state = s ? s : 0xACE1U;        /* zero is a fixed point: refuse it */
}

void rnd_stir (void)
{
    rnd_state ^= (unsigned int)TED_RASTER_LO;
    rnd_state += (unsigned int)TED_RASTER_HI;
    rnd_state ^= (unsigned int)(TED_TIMER1_LO) << 3;
    if (!rnd_state) rnd_state = 0xACE1U;
}

unsigned char rnd_byte (void)
{
    /* xorshift-16 with the (7, 9, 8) triple - full period, and three
    ** shifts is about as cheap as randomness gets on a 6502. */
    rnd_state ^= (unsigned int)(rnd_state << 7);
    rnd_state ^= (unsigned int)(rnd_state >> 9);
    rnd_state ^= (unsigned int)(rnd_state << 8);
    return (unsigned char)(rnd_state >> 8);
}

unsigned char rnd_below (unsigned char n)
{
    if (n < 2) return 0;
    return (unsigned char)(rnd_byte () % n);
}

/* --- drawing one lot -------------------------------------------------- */

static void lot_draw (unsigned char k)
{
    unsigned char  x = lx[k], y = ly[k];
    unsigned char* r;
    unsigned char* c;

    DBG_BOUND (DBG_DIC, "drawy", y + 2, SCR_H);
    DBG_BOUND (DBG_DIC, "drawx", x + 2, SCR_W);

    r = rowtab[y] + x;
    c = r - SCREEN_COLOR_D;

    /* THE BLANK CELLS ARE NOT DRAWN.
    **
    ** A lot is a 3x3 block of which four cells are empty, and this used to
    ** write CH_SPACE into them.  That is what chopped the corners off the
    ** neighbours: two lots close together, and one lot's empty corner
    ** blanked a cell belonging to the other's apex or diagonal.  Nothing
    ** here writes a cell it does not own any more - the floor is cleared
    ** wholesale before the lots are painted (see lots_paint), so there is
    ** nothing left over for the blanks to have to cover. */
    if (lot_tip[k]) { r[1] = CH_DISC; c[1] = thrower_colour; }
    else            { r[1] = CH_RING; c[1] = CBYTE (4, 1); }

    r = rowtab[y + 1] + x;  c = r - SCREEN_COLOR_D;
    r[0] = CH_DIAG_R; c[0] = CBYTE (6, 1);
    r[2] = CH_DIAG_L; c[2] = CBYTE (6, 1);

    blit_ptr = rowtab[y + 2] + x;
    blit_ch  = CH_SOLID;
    blit_cl  = CBYTE (5, 1);            /* bone white */
    blit_run (3);

    ex[k] = x;
    ey[k] = y;
}

static void lot_erase (unsigned char k)
{
    unsigned char i;

    blit_ch = CH_SPACE;
    blit_cl = 0;
    DBG_BOUND (DBG_DIC, "ery", ey[k] + 2, SCR_H);
    DBG_BOUND (DBG_DIC, "erx", ex[k] + 2, SCR_W);
    for (i = 0; i < 3; ++i) {
        blit_ptr = rowtab[ey[k] + i] + ex[k];
        blit_run (3);
    }
}

static unsigned char home_x (unsigned char k)
{
    return (unsigned char)(5 + k * 8);
}

static void lot_home (unsigned char k)
{
    lx[k] = home_x (k);
    ly[k] = LOT_HOME_ROW;
}

/* Erase every lot, then draw every lot.
**
** One at a time was the other half of the corner-chopping: erasing lot k's
** 3x3 block also erased whatever of lot j happened to be inside it, and
** lot j was not redrawn.  Painting all four every time costs 36 cells and
** makes overlap a purely visual matter that resolves itself on the next
** frame, rather than damage that persists. */
static void lots_erase (void)
{
    unsigned char k;

    for (k = 0; k < LOTS; ++k) lot_erase (k);
}

static void lots_paint (void)
{
    unsigned char k;

    for (k = 0; k < LOTS; ++k) lot_draw (k);
}

void floor_wipe (void)
{
    if (no_draw) return;
    rows_fill (FLOOR_TOP, FLOOR_BOT, CH_SPACE, 0);
    /* FLOOR_BOT is the chronicle's top line: the lots are allowed to roll
    ** over the log, and clearing the floor therefore clears a line that
    ** still has words on it.  They are in the chronicle's own buffer, so
    ** putting them back costs one repaint. */
    chronicle_redraw ();
}

/* --- the throw -------------------------------------------------------- */

unsigned char throw_quiet (void)
{
    unsigned char k, r = 0;

    for (k = 0; k < LOTS; ++k) {
        lot_tip[k] = (unsigned char)(rnd_byte () & 1);
        r = (unsigned char)(r + lot_tip[k]);
    }
    return r;
}

void lots_show (unsigned char tips)
{
    unsigned char k;

    if (no_draw) return;
    for (k = 0; k < LOTS; ++k) {
        lot_tip[k] = (unsigned char)(k < tips);
        lot_home (k);
        lot_draw (k);
    }
}

/* Reject a scattered position that lands on top of another lot.  Each lot
** is placed against the others' CURRENT positions, and later placements
** avoid earlier finalised ones, so every pair ends up checked. */
static unsigned char overlaps (unsigned char k)
{
    unsigned char j, dx, dy;

    for (j = 0; j < LOTS; ++j) {
        if (j == k) continue;
        dx = (unsigned char)(lx[k] > lx[j] ? lx[k] - lx[j] : lx[j] - lx[k]);
        dy = (unsigned char)(ly[k] > ly[j] ? ly[k] - ly[j] : ly[j] - ly[k]);
        if (dx < 4 && dy < 3) return 1;
    }
    return 0;
}

/* --- the walk ---------------------------------------------------------
**
** A lot no longer teleports to a fresh random cell each frame.  It starts
** somewhere random on the floor and WALKS to its counting slot, which is
** what a thrown die does: it travels, it is deflected by whatever is in
** the way, and it arrives.
**
** The eight compass steps in order, so that turning 45 degrees is +/-1
** around this table and 90 degrees is +/-2.  That is the whole reason the
** table is in this order rather than any other. */
static const signed char step_dx[8] = {  0,  1, 1, 1, 0, -1, -1, -1 };
static const signed char step_dy[8] = { -1, -1, 0, 1, 1,  1,  0, -1 };

/* Which of the eight points roughly at the destination. */
static unsigned char dir_toward (unsigned char k, unsigned char tx,
                                 unsigned char ty)
{
    signed char dx = (signed char)(tx - lx[k]);
    signed char dy = (signed char)(ty - ly[k]);

    if (dy < 0) return (unsigned char)(dx < 0 ? 7 : (dx > 0 ? 1 : 0));
    if (dy > 0) return (unsigned char)(dx < 0 ? 5 : (dx > 0 ? 3 : 4));
    return (unsigned char)(dx < 0 ? 6 : 2);
}

/* Try to move `dist` cells along compass direction `d`.  Leaves the lot
** where it was and reports failure if that would go off the floor or land
** on another lot. */
static unsigned char try_step (unsigned char k, unsigned char d,
                               unsigned char dist)
{
    signed int    nx = (signed int)lx[k] + (signed int)step_dx[d] * dist;
    signed int    ny = (signed int)ly[k] + (signed int)step_dy[d] * dist;
    unsigned char ox = lx[k], oy = ly[k];

    /* A lot is three wide and three tall, so its origin has to stay clear
    ** of the right and bottom edges by two. */
    if (nx < 1 || nx > (signed int)(SCR_W - 4)) return 0;
    if (ny < (signed int)FLOOR_TOP || ny > (signed int)(FLOOR_BOT - 2)) return 0;

    lx[k] = (unsigned char)nx;
    ly[k] = (unsigned char)ny;
    if (overlaps (k)) { lx[k] = ox; ly[k] = oy; return 0; }
    return 1;
}

/* One step of the walk toward (tx, ty).
**
** Straight on first; if that is blocked, angle away by 45 degrees to
** either side, then by 90.  Each angle is tried at the near distance and
** then one cell further out, because a lot that is boxed in at one cell
** can often clear the obstruction in two.  Every candidate is a point
** between where it is and where it is going, so a deflected lot still
** makes progress rather than wandering. */
static void walk_toward (unsigned char k, unsigned char tx, unsigned char ty)
{
    static const signed char turn[5] = { 0, 1, -1, 2, -2 };
    unsigned char base = dir_toward (k, tx, ty);
    unsigned char reach = (unsigned char)(1 + rnd_below (2));
    unsigned char i, d;

    for (i = 0; i < 5; ++i) {
        d = (unsigned char)((base + 8 + turn[i]) & 7);
        if (try_step (k, d, reach)) return;
        if (try_step (k, d, (unsigned char)(reach + 1))) return;
    }
    /* Boxed in on all five headings at both distances: stand still this
    ** frame rather than jump somewhere unreachable. */
}

/* Somewhere on the floor to be thrown from. */
static void scatter (unsigned char k)
{
    unsigned char tries = 0;

    do {
        lx[k] = (unsigned char)(1 + rnd_below (SCR_W - 4));
        ly[k] = (unsigned char)(FLOOR_TOP + rnd_below (FLOOR_BOT - FLOOR_TOP - 1));
    } while (overlaps (k) && ++tries < 25);
}

unsigned char throw_lots (void)
{
    unsigned char frames, f, k, still, tips = 0;

    DBG_ENTER (DBG_DIC, "throw");
    if (no_draw) { DBG_LEAVE (); return throw_quiet (); }

    /* Thrown from somewhere random on the floor, not from the counting
    ** slots - the lots have to travel to be seen to travel. */
    for (k = 0; k < LOTS; ++k) {
        settled[k] = 0;
        scatter (k);
        ex[k] = lx[k];
        ey[k] = ly[k];
        lot_tip[k] = (unsigned char)(rnd_byte () & 1);
    }
    lots_paint ();

    /* How long a throw takes is a decision rather than a consequence.  The
    ** BASIC edition managed one to three tumble frames because each one
    ** cost a fortune in POKEs; compiled, a frame is under a millisecond, so
    ** the number is set by what a thrown die looks like.
    **
    ** Sixteen to twenty-one steps at TUMBLE_PACE apart is 0.6 to 0.8
    ** seconds of travel before the first lot settles. */
    frames = (unsigned char)(16 + rnd_below (6));
    for (f = 0; f < frames; ++f) {
        still = 0;
        for (k = 0; k < LOTS; ++k) if (!settled[k]) ++still;
        if (!still) break;

        /* Erase all, move, paint all - so a lot passing close to another
        ** cannot leave a bite out of it. */
        lots_erase ();
        for (k = 0; k < LOTS; ++k) {
            if (settled[k]) continue;
            walk_toward (k, home_x (k), LOT_HOME_ROW);
            lot_tip[k] = (unsigned char)(rnd_byte () & 1);
        }
        lots_paint ();
        sfx (SFX_CLICK);
        wait_frames_live (TUMBLE_PACE);

        /* Past the halfway point, lots begin to come to rest - by which
        ** time the walk has carried them most of the way to the slot they
        ** settle into, so settling is a short hop and not a jump across
        ** the floor. */
        if (f >= (frames >> 1)) {
            for (k = 0; k < LOTS; ++k) {
                if (settled[k] || rnd_below (3) != 0) continue;
                lots_erase ();
                settled[k] = 1;
                lot_home (k);
                lot_tip[k] = (unsigned char)(rnd_byte () & 1);
                lots_paint ();
                sfx (SFX_SETTLE);
                wait_frames_live (SETTLE_PACE);
                break;                  /* one at a time reads as dice */
            }
        }
    }

    for (k = 0; k < LOTS; ++k) {
        if (!settled[k]) {
            lots_erase ();
            settled[k] = 1;
            lot_home (k);
            lot_tip[k] = (unsigned char)(rnd_byte () & 1);
            lots_paint ();
            sfx (SFX_SETTLE);
            wait_frames_live (SETTLE_PACE);
        }
        tips = (unsigned char)(tips + lot_tip[k]);
    }
    DBG_VAL (DBG_DIC, "tips", tips);
    DBG_LEAVE ();
    return tips;
}
