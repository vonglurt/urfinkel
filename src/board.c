/* ------------------------------------------------------------------------
 * board.c - the BASIC edition's renderer, compiled.
 *
 * Every constant here is lifted from urroyal.bas rather than re-derived,
 * because the acceptance test for this file is a screenshot diff against
 * the frozen edition.  Where a line number is quoted in a comment it names
 * the BASIC subroutine this code replaces.
 *
 * The one structural change is the row-address table.  BASIC hoisted row
 * bases into variables by hand (`ZA=SC+800`) because it re-evaluates
 * `20*40` in floating point on every iteration; here the whole column of
 * 25 row addresses is built once at startup, so no multiply by 40 survives
 * into any drawing loop.
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "rules.h"
#include "board.h"
#include "music.h"
#include "kbd.h"
#include "dbg.h"

#ifdef __CC65__
#  include <cbm.h>                      /* cbm_k_bsout: the KERNAL CHROUT */
#endif

/* --- presentation state (the game state itself lives in rules.c) ------ */

unsigned char player_hue[2]  = { 9, 7 };
unsigned char player_lum[2]  = { 6, 5 };
const char*   player_name[2] = { "alpha", "beta" };

/* --- whose turn it is, as light ---------------------------------------
**
** The board is three bands: the top four rows are player one's own
** squares, the bottom four are player two's, and the middle four are the
** bridge, which belongs to both.  Raising player_dim[q] takes that
** player's band and their waist plaque down three luminance steps, so the
** side that is not to move recedes and the side that is stands in full
** colour with its stats legible.
**
** It is done here rather than at the call sites because EVERY repaint has
** to respect it - a piece landing on a dimmed square must land dimmed -
** and paint_button is the one place every square goes through.
**
** Both default to 0, which is why demo.c - the conformance oracle - draws
** the frozen edition's board without knowing this exists. */

unsigned char player_dim[2];

#define DIM_DROP        3

static unsigned char band_dim (unsigned char y)
{
    if (y == band_y[0]) return player_dim[0];
    if (y == band_y[2]) return player_dim[1];
    return 0;                           /* the bridge is nobody's alone   */
}

static unsigned char dim_lum (unsigned char l)
{
    return (unsigned char)(l > DIM_DROP ? l - DIM_DROP : 0);
}

static unsigned char dim_col (unsigned char c)
{
    return CBYTE (dim_lum ((unsigned char)(c >> 4)), (unsigned char)(c & 0x0F));
}

/* --- tables ----------------------------------------------------------- */

/* WHERE THE THREE BANDS START.
**
** The board used to be twelve rows of four - bands butted straight up
** against each other, so the only thing between one row of tiles and the
** next was the tiles' own shadow row. That reads as one slab with lines
** drawn on it. It is fourteen rows now, with a black row between the
** bands, and the black is what makes each band read as a separate shelf.
**
** The pitch is a table and not an expression because it is no longer
** uniform: bands are four rows apart at the top and five thereafter. Every
** part of the renderer that used to compute `ri * 4` reads this instead,
** and the two extra rows are paid for out of the casting floor - see
** FLOOR_TOP in dice.h, which the lots now share with the chronicle's top
** line. The screen is forty by twenty-five and nothing is free. */
const unsigned char band_y[3] = { 0, 5, 10 };   /* BOARD_ROWS in board.h */

unsigned char* rowtab[SCR_H];
unsigned char  sq_x[2][SQ_HOME];
unsigned char  sq_y[2][SQ_HOME];
unsigned char  sq_c[2][SQ_HOME];

static void hl_tab_build (void);        /* see "the shimmer", below      */

/* BASIC 130-200: the path, laid onto the screen.  Squares 1-4 run
** backwards along the player's own outer row, 5-12 forward along the
** shared bridge, 13-14 back along the outer row again. */
static void geometry_init (void)
{
    unsigned char a, s, ci, ri, b;

    for (a = 0; a < 2; ++a) {
        for (s = 1; s <= PATH_LAST; ++s) {
            if (s < 5)                  { ci = (unsigned char)(5 - s); ri = (unsigned char)(a * 2); }
            else if (s < 13)            { ci = (unsigned char)(s - 4); ri = 1; }
            else if (s == 13)           { ci = 8; ri = (unsigned char)(a * 2); }
            else                        { ci = 7; ri = (unsigned char)(a * 2); }

            sq_x[a][s] = (unsigned char)((ci - 1) * 5);
            sq_y[a][s] = band_y[ri];

            b = ((ci + ri) & 1) ? 87 : 72;          /* the checkerboard   */
            if (rosette[s]) b = 107;                /* rosettes are gold  */
            sq_c[a][s] = b;
        }
    }
}

void board_init (void)
{
    unsigned char  i;
    unsigned char* p = SCREEN;

    for (i = 0; i < SCR_H; ++i) {
        rowtab[i] = p;
        p += SCR_W;
    }
    geometry_init ();
    hl_tab_build ();

#ifdef __CC65__
    /* The board is drawn in the upper case / graphics character set, the
    ** one a Plus/4 boots into and the one the BASIC edition assumed.  Its
    ** codes 87, 81 and 170 are the waiting ring, the home disc and the
    ** rosette's reverse star; in the lower case set cc65's startup selects
    ** they are the letters w and q and a plain asterisk. */
    cbm_k_bsout (CHARSET_UPPER);
#endif
}

/* Letters to screen codes.  Deliberately written so it holds whether the
** compiler translated the literal to ASCII or to PETSCII: unshifted
** letters are 65-90 in PETSCII and 97-122 in ASCII, and both ranges want
** to become 1-26.  Digits and space are the same code in all three sets. */
unsigned char scr_code (unsigned char c)
{
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a' + 1);
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A' + 1);
    return c;
}

/* --- primitives ------------------------------------------------------- */

/* Any full-screen wipe means whatever was on it is gone, the board
** included, so the shimmer stops here rather than at twenty call sites
** that would each have to remember. */
void screen_fill (unsigned char ch, unsigned char cl)
{
    shimmer_enable (0);
    rows_fill (0, SCR_H - 1, ch, cl);
}

void rows_fill (unsigned char y0, unsigned char y1,
                unsigned char ch, unsigned char cl)
{
    unsigned char i;

    blit_ch = ch;
    blit_cl = cl;
    for (i = y0; i <= y1; ++i) {
        blit_ptr = rowtab[i];
        blit_run (SCR_W);
    }
}

/* --- the shading profile ---------------------------------------------- */
/* How far the four rows of a tile sit from its base luminance.  This used
** to be four numbers written into paint_button, which was fine while the
** only requirement was to match the frozen edition exactly.  It is a table
** now because there are two answers:
**
**   LEGACY   +2 / -3 / -1   the frozen edition's, and the ONLY profile
**                           `make conform` will accept, because that test
**                           asserts a byte-identical screenshot
**   SOFT     +1 / -2 / -1   what the game actually plays in
**
** The legacy tile spends FIVE of the machine's eight usable luminance
** steps on one 4x4 square - highlight 7 down to shadow 2 on the gold - and
** at that spread a bevel stops reading as a lit surface and starts reading
** as three stripes.  The soft profile halves it to three steps, which is
** enough for the eye to place the light source and no more.  The room it
** gives back is what the shimmer (below) moves around in.
**
** SHADE_LEGACY and SHADE_SOFT are declared in board.h, because the caller
** that chooses between them is main(). */

struct shade {
    unsigned char hi;                   /* the highlight row, added       */
    unsigned char lo;                   /* the shadow row, subtracted     */
    unsigned char well;                 /* the two-column well, subtracted */
    unsigned char sunk;                 /* the bridge's inverted top edge  */
    unsigned char drop;                 /* the whole band, subtracted     */
};

/* `drop` takes the WHOLE tile down before the bevel is derived from it, so
** highlight, base, well and shadow all move together and the tile keeps
** its shape while getting darker. Dropping only the base would flatten it
** instead, which is the opposite of what a darker tile is for: the black
** grid around it only reads as an edge if the thing inside the edge is
** dark enough for black to be a step down from it. */
static const struct shade shade_tab[2] = {
    { 2, 3, 1, 2, 0 },                  /* legacy: the frozen edition's   */
    { 1, 2, 1, 1, 2 },                  /* soft: what the game plays in   */
};

static unsigned char shade_which = SHADE_LEGACY;

/* The tile painter - BASIC 7200.
**
**   highlight row   luminance + profile.hi, capped at 7
**   two base rows   the base luminance, with a two-column well
**   shadow row      luminance - profile.lo, floored at 0
**
** except on the bridge (the only band at row 4), whose top edge inverts so
** the shared corridor reads as a sunken lane rather than a raised key.
** That single condition is the whole difference between the two kinds of
** square. */
void paint_button (unsigned char x, unsigned char y, unsigned char base,
                   unsigned char well, unsigned char flood, unsigned char pcol)
{
    const struct shade* sh = &shade_tab[shade_which];
    unsigned char  tl = base >> 4;              /* base luminance */
    unsigned char  th = base & 0x0F;            /* hue            */
    unsigned char  hl, sl, wl, pb, cw, ch, j;
    unsigned char* r;

    /* The side that is not to move recedes.  Done first, so everything
    ** derived below - highlight, shadow, well, occupant - recedes with it
    ** rather than only the flat parts. */
    /* The profile's drop first, then the turn dimming on top of it: both
    ** are luminance subtractions and they compose, so a waiting side's
    ** tile is the dark tile taken further down rather than a different
    ** tile. */
    tl = (unsigned char)(tl >= sh->drop ? tl - sh->drop : 0);

    if (band_dim (y)) {
        tl   = dim_lum (tl);
        pcol = dim_col (pcol);
    }

    /* band_y[1], not a literal 4: the bridge moved down a row when the
    ** black separators went in, and a hardcoded row here would have
    ** silently given the sunken top edge to the wrong band. */
    if (y == band_y[1]) hl = (tl >= sh->sunk) ? (unsigned char)(tl - sh->sunk) : 0;
    else        hl = (unsigned char)(tl + sh->hi <= 7 ? tl + sh->hi : 7);
    sl = (tl >= sh->lo)   ? (unsigned char)(tl - sh->lo)   : 0;
    wl = (tl >= sh->well) ? (unsigned char)(tl - sh->well) : 0;

    blit_ch  = CH_SOLID;
    blit_ptr = rowtab[y] + x;
    blit_cl  = CBYTE (hl, th);
    blit_run (4);

    blit_ptr = rowtab[y + 3] + x;
    blit_cl  = CBYTE (sl, th);
    blit_run (4);

    pb = flood ? pcol : CBYTE (tl, th);
    cw = flood ? pcol : CBYTE (wl, th);
    ch = CH_SOLID;
    if (well == 1) {                            /* a rosette fills its well */
        ch = CH_STAR;
        cw = band_dim (y) ? CBYTE (4, 7) : CBYTE (7, 7);
    }

    for (j = 1; j <= 2; ++j) {
        r = rowtab[y + j] + x;
        blit_ch = CH_SOLID;  blit_cl = pb;  blit_ptr = r;      blit_run (1);
        blit_ch = ch;        blit_cl = cw;  blit_ptr = r + 1;  blit_run (2);
        blit_ch = CH_SOLID;  blit_cl = pb;  blit_ptr = r + 3;  blit_run (1);
    }

    if (well > 1) {                             /* an occupant or a number  */
        r = rowtab[y + 1] + x + 1;
        *r = well;
        *(r - SCREEN_COLOR_D) = pcol;
    }
}

/* The board table - BASIC 1200.  Only the gutter columns are painted; the
** buttons cover everything else, so this is 90 cells instead of 306. */
void paint_table (void)
{
    unsigned char ri, y, gi, x;

    blit_ch = CH_SOLID;
    /* BLACK, not brown at luminance 0. Luminance 0 of a hue is the darkest
    ** that hue goes, and it is not black - against a black background it
    ** still reads as a coloured line. Hue 0 is the only true black TED
    ** has, and a true black gutter is what turns a grid of tiles into
    ** tiles with edges. */
    blit_cl = CBYTE (0, 0);
    for (ri = 0; ri < 3; ++ri) {
        for (y = band_y[ri]; y < (unsigned char)(band_y[ri] + 4); ++y) {
            for (gi = 0; gi < 8; ++gi) {
                x = (unsigned char)(4 + gi * 5);
                if (ri != 1 && x > 18 && x < 30) continue;   /* the waist    */
                blit_ptr = rowtab[y] + x;
                blit_run (1);
            }
        }
    }
}

/* The twenty buttons - BASIC 1220. */
void paint_buttons (void)
{
    unsigned char ri, ci, base, well;

    for (ri = 0; ri < 3; ++ri) {
        for (ci = 1; ci <= 8; ++ci) {
            if (ri != 1 && ci > 4 && ci < 7) continue;       /* the waist    */
            base = ((ci + ri) & 1) ? 87 : 72;                /* checkerboard */
            well = 0;
            if ((ri != 1 && (ci == 1 || ci == 7)) || (ri == 1 && ci == 4)) {
                base = 107;                                  /* rosette gold */
                well = 1;
            }
            paint_button ((unsigned char)((ci - 1) * 5),
                          band_y[ri], base, well, 0, 0);
        }
    }
}

/* --- the shimmer ------------------------------------------------------ */
/*
** A GLINT THAT ORBITS EACH TILE, not a cloud crossing the board.
**
** The first version was a vertical band travelling left to right, which is
** what a spotlight on a flat sheet does and looked like it.  The second
** bent it into a chevron - `x + 2 * |y - vein|` - so the front ran out
** along a fold like light crossing a leaf.  Better, and still a weather
** system: something passing OVER the board rather than anything to do with
** the tiles themselves.
**
** The third version orbited: a point running round each tile's face, out
** along the top edge and back along the bottom.  It read as something
** crawling round the rim rather than as light on the face, and the phase
** offsets that were meant to make the crests drift across the board did
** not line up with the tile pitch, so the drift broke at every boundary -
** the crest jumping a gap or doubling back instead of running on.
**
** THIS ONE DRIFTS, AND IN TWO DIRECTIONS.  The glint is a vertical PAIR -
** a tile's highlight cell and its shadow cell in the same column - moving
** horizontally along the face.  Both edges light together and travel
** together, which is what makes it a sheen crossing the tile.
**
** The two OUTER bands run LEFT and the MIDDLE band runs RIGHT, so the
** board carries two currents passing one another.  That is also the
** reading the board wants: the outer rows are each player's own track and
** the middle is the corridor they share and fight over.
**
** ALL AT ONCE, OUT OF PHASE.  Every tile runs the same eight-step cycle
** but starts at a different point in it, and the offset is chosen so the
** light runs on UNBROKEN from one tile into the next - see tile_phase,
** where the multiplier is arithmetic rather than taste.
**
** WHICH CELLS, AND WHY ONLY THOSE.  The top row and the bottom row of each
** tile - the highlight and the shadow - and never the two rows between
** them.  That is not tidiness: the middle rows carry the well, and the
** well carries pieces, rosette stars, and the flood colour of whoever is
** standing on the square.  A glint that crossed the full face would
** have to restore those cells from a resting table, and would therefore
** repaint a piece's own colour out from under it twice a second.  Running
** the top edge one way and the bottom edge the other circles the face
** without ever touching what is standing on it.
**
** COLOUR RAM ONLY.  The screen matrix never changes, so the shimmer can
** never disturb a glyph - the worst it can do is get a colour wrong for
** one frame.  Same discipline as the marquee's lamp chase.
**
** ONLY UPWARDS, AND ONLY ON THE ACTIVE SIDE.  It adds to a luminance and
** never subtracts, so a tile is never darker than the shading profile says
** it is; and a dimmed band is skipped entirely, so the shimmer is also the
** answer to "whose turn is it".
*/

#define SHIM_PACE       3               /* frames per step of the drift   */
#define SHIM_STEPS      8               /* the ring the phase runs round  */
#define SHIM_SPAN       4               /* a tile is four cells wide      */
#define SHIM_LIFT       2               /* luminance added at the glint   */
#define SHIM_TRAIL      1               /* ...and the cell it just left   */

static unsigned char hl_tab[3][8];      /* resting highlight, 255 = waist */
static unsigned char sh_tab[3][8];      /* resting shadow                 */
static unsigned char shimmer_on;
static unsigned char shim_p;            /* where the drift has got to     */
static unsigned char shim_wait;

/* The resting highlight and shadow colours of every tile, by the same
** rules paint_buttons uses to decide what each one is.  Built once and
** kept in step with the shading profile rather than recomputed per frame:
** the per-frame cost is then a lookup, an add, a clamp and a store. */
static void hl_tab_build (void)
{
    const struct shade* sh = &shade_tab[shade_which];
    unsigned char ri, ci, base, tl, th, hl, sl;

    for (ri = 0; ri < 3; ++ri) {
        for (ci = 1; ci <= 8; ++ci) {
            if (ri != 1 && ci > 4 && ci < 7) {          /* the waist       */
                hl_tab[ri][ci - 1] = 255;
                sh_tab[ri][ci - 1] = 255;
                continue;
            }
            base = ((ci + ri) & 1) ? 87 : 72;
            if ((ri != 1 && (ci == 1 || ci == 7)) || (ri == 1 && ci == 4))
                base = 107;
            tl = (unsigned char)(base >> 4);
            th = (unsigned char)(base & 0x0F);
            /* The same drop paint_button applies, or the shimmer would
            ** rest every tile back to the colour it had before the band
            ** was darkened and undo it one cell at a time. */
            tl = (unsigned char)(tl >= sh->drop ? tl - sh->drop : 0);
            if (ri == 1) hl = (tl >= sh->sunk) ? (unsigned char)(tl - sh->sunk) : 0;
            else         hl = (unsigned char)(tl + sh->hi <= 7 ? tl + sh->hi : 7);
            sl = (tl >= sh->lo) ? (unsigned char)(tl - sh->lo) : 0;
            hl_tab[ri][ci - 1] = CBYTE (hl, th);
            sh_tab[ri][ci - 1] = CBYTE (sl, th);
        }
    }
}

/* WHICH WAY THE LIGHT TRAVELS ALONG A BAND.  The two outer bands run
** LEFT and the middle band runs RIGHT, so the board reads as two currents
** passing one another rather than as one sweep - which is also the reading
** the board wants, the outer rows being each player's own track and the
** middle one the corridor they share and contest. */
#define BAND_LEFTWARD(ri)       ((ri) != 1)

/* Where this tile is in the cycle.
**
** THE MULTIPLIER IS NOT A TASTE SETTING.  A tile is four cells wide on a
** five-column pitch, and the crest moves one cell a step, so for the light
** to run on unbroken from one tile into the next the tile to the right has
** to begin five steps LATER.  In an eight-step ring "five later" is "three
** earlier", which is where the 3 comes from - and reversing its sign, to
** five, is the whole of what reverses the direction of travel.
**
** Get this wrong and the drift is not merely in the wrong direction: the
** crest jumps a gap or doubles back on itself at every tile boundary,
** which is what "the drift" looked like before.
**
** The `ri * 5` keeps the three bands out of lockstep, so the two outer
** ones do not pulse as a single block. */
static unsigned char tile_phase (unsigned char ri, unsigned char ci)
{
    unsigned char c = BAND_LEFTWARD (ri) ? (unsigned char)(ci * 5)
                                         : (unsigned char)(ci * 3);
    return (unsigned char)((c + ri * 5) & (SHIM_STEPS - 1));
}

/* One cell of one edge, lifted by `lift` above its resting colour. */
static void glint (unsigned char y, unsigned char x, unsigned char c,
                   unsigned char lift)
{
    unsigned char* cm = rowtab[y] - SCREEN_COLOR_D;
    unsigned char  l;

    if (!lift) { cm[x] = c; return; }
    l = (unsigned char)((c >> 4) + lift);
    if (l > 7) l = 7;
    cm[x] = (unsigned char)((l << 4) | (c & 0x0F));
}

/* One tile's edge cells, for the phase it is currently at.
**
** The glint is a VERTICAL PAIR - the highlight cell and the shadow cell of
** the same column - travelling along the tile, rather than a single point
** going round it.  Both edges therefore light together and move together,
** which is what makes it read as a sheen crossing the face rather than as
** something crawling round the rim.
**
** All four columns are written every step rather than only the ones that
** changed: four stores against the bookkeeping to know which, on a tile
** four cells wide, is the cheaper of the two and cannot drift out of step
** with itself.  Same discipline as before.
**
** THE TAIL IS BEHIND THE CREST, which it was not before.  The old code lit
** `d == 1`, and since the crest advances along increasing p that cell is
** the one it is about to reach - a glow running AHEAD of the light.  The
** cell just vacated is `d == SHIM_STEPS - 1`. */
static void drift_tile (unsigned char y, unsigned char x,
                        unsigned char hc, unsigned char sc,
                        unsigned char phase, unsigned char leftward)
{
    unsigned char ox, step, d, lift;

    for (ox = 0; ox < SHIM_SPAN; ++ox) {
        /* Which step of the cycle lights THIS column.  Running the column
        ** index backwards is all that reverses the travel. */
        step = leftward ? (unsigned char)(SHIM_SPAN - 1 - ox) : ox;
        d = (unsigned char)((step + SHIM_STEPS - phase) & (SHIM_STEPS - 1));
        if      (d == 0)                lift = SHIM_LIFT;
        else if (d == SHIM_STEPS - 1)   lift = SHIM_TRAIL;
        else                            lift = 0;

        glint (y, (unsigned char)(x + ox), hc, lift);
        glint ((unsigned char)(y + 3), (unsigned char)(x + ox), sc, lift);
    }
}

/* Choosing a profile rebuilds the shimmer's resting tables, because they
** are derived from it: leave them stale and the shimmer restores every
** tile to the colour the OTHER profile gave it, one cell at a time. */
void shade_profile (unsigned char p)
{
    shade_which = (unsigned char)(p > SHADE_SOFT ? SHADE_LEGACY : p);
    hl_tab_build ();
}

void shimmer_enable (unsigned char on)
{
    shimmer_on = on;
    shim_p     = 0;
    shim_wait  = 0;
}

void shimmer_frame (void)
{
    unsigned char ri, ci, x, y;

    if (!shimmer_on) return;
    if (++shim_wait < SHIM_PACE) return;
    shim_wait = 0;
    shim_p = (unsigned char)((shim_p + 1) & (SHIM_STEPS - 1));

    for (ri = 0; ri < 3; ++ri) {
        y = band_y[ri];
        /* The side that is not to move does not catch the light.  That is
        ** the shimmer doing double duty: it is decoration and it is also
        ** the answer to whose turn it is. */
        if (band_dim (y)) continue;
        for (ci = 0; ci < 8; ++ci) {
            if (hl_tab[ri][ci] == 255) continue;
            x = (unsigned char)(ci * 5);
            drift_tile (y, x, hl_tab[ri][ci], sh_tab[ri][ci],
                        (unsigned char)((shim_p + tile_phase (ri, ci))
                                        & (SHIM_STEPS - 1)),
                        BAND_LEFTWARD (ri));
        }
    }
}

/* One board cell with whatever stands on it - BASIC 7000.
**
** The renderer asks "which pieces stand on this square" at draw time
** rather than caching it, which is why game state and screen state can
** never disagree.  An occupied square floods both base rows with the
** owner's colour and cuts the piece number out of it. */
void draw_cell (unsigned char player, unsigned char square)
{
    square_tint (player, square, sq_c[player][square], 0);
}

/* The same cell, painted in a colour of the caller's choosing rather than
** the one the board says it is.  This is what the move animation fades
** with: a square is repainted a dozen times on its way from black through
** white to whoever now owns it, and every one of those repaints has to
** keep the piece standing on it, the rosette star in it and the flood
** treatment of its occupant.  Doing that by re-deriving the occupant each
** time - rather than by caching a "before" image - is the same choice the
** renderer makes everywhere else: ask the game state, never a copy of it,
** and screen and state cannot drift apart.
**
** `pcol` of 0 means "leave the occupant its own colour"; anything else
** overrides it, which is how a captured square's piece can be dragged
** through red and white along with the tile under it. */
void square_tint (unsigned char player, unsigned char square,
                  unsigned char base, unsigned char pcol)
{
    unsigned char well  = rosette[square] ? 1 : 0;
    unsigned char flood = 0;
    unsigned char own   = 0;
    unsigned char q, j;

    for (q = 0; q < 2; ++q) {
        for (j = 1; j <= PIECES; ++j) {
            if (piece[q][j] != square) continue;
            /* In the shared corridor either player's piece shows; on the
            ** private squares only the owner's, because the two players'
            ** square N are different cells that happen to share a number. */
            if ((square >= SHARED_FIRST && square <= SHARED_LAST) || q == player) {
                well  = (unsigned char)(CH_PIECE_BASE + j);
                own   = CBYTE (player_lum[q], (unsigned char)(player_hue[q] - 1));
                flood = 1;
            }
        }
    }

    paint_button (sq_x[player][square], sq_y[player][square],
                  base, well, flood, pcol ? pcol : own);
}

/* --- the waist plaques (BASIC 7700-7799) ------------------------------ */

/* Engrave one centred line on plaque row y: the plate is the player's own
** hue at luminance `lum`, and the letters are reverse video so the black
** background shows through their strokes.  BASIC 7760. */
/* The plaque spans columns 19-29.  The NAME does not: it sits on 21-27,
** the same seven columns the token row below it uses, leaving two black
** columns at each end.
**
** Two reasons, and the second is the one that matters.  The margin gives
** the coloured field an edge, the way every tile on this board now has
** one.  And the name lining up with the pieces underneath it makes the
** plaque read as one object - a label over its own row of tokens - rather
** than as two bars of different widths that happen to be adjacent. */
#define PLAQUE_X        19
#define PLAQUE_W        11
#define NAME_X          21              /* PLAQUE_X + 2                   */
#define NAME_W          7               /* PLAQUE_W - 4, and the token row */

static void plaque_engrave (unsigned char y, const char* s,
                            unsigned char lum, unsigned char q)
{
    unsigned char  n = 0, i, x0;
    unsigned char* r = rowtab[y] + NAME_X;

    /* The margins first, so they are black whatever was there before. */
    blit_ch  = CH_SOLID;
    blit_cl  = CBYTE (0, 0);
    blit_ptr = rowtab[y] + PLAQUE_X;
    blit_run (NAME_X - PLAQUE_X);
    blit_ptr = rowtab[y] + NAME_X + NAME_W;
    blit_run (PLAQUE_X + PLAQUE_W - NAME_X - NAME_W);

    blit_ptr = r;
    blit_ch  = CH_SOLID;
    /* Floored at 2, not 0: the side that is waiting should recede, not
    ** disappear.  Its name and its piece count are still information the
    ** other player needs. */
    if (player_dim[q]) {
        lum = dim_lum (lum);
        if (lum < 2) lum = 2;
    }
    blit_cl  = CBYTE (lum, (unsigned char)(player_hue[q] - 1));
    blit_run (NAME_W);

    if (s == 0) return;
    while (s[n]) ++n;
    /* Truncated rather than shrunk to fit: seven columns is what there is,
    ** and a name that overflows is cut at seven with the first seven
    ** letters kept, which is the part a player recognises. */
    if (n > NAME_W) n = NAME_W;
    x0 = (unsigned char)((NAME_W - n) >> 1);
    for (i = 0; i < n; ++i)
        r[x0 + i] = (unsigned char)(scr_code ((unsigned char)s[i]) | CH_REVERSE);
}

/* Clear one waist row to black - no plate, no glyphs.  BASIC 7775. */
static void plaque_clear (unsigned char y)
{
    blit_ptr = rowtab[y] + PLAQUE_X;
    blit_ch  = CH_SPACE;
    blit_cl  = 0;
    blit_run (PLAQUE_W);
}

/* The two columns the plaque shares with the tiles either side of it.
**
** In the outer bands the gutter painter skips 19..29 - that span is the
** plaque, not board - so the plaque used to run straight into the tile
** next to it with nothing between them.  Everywhere else on this board a
** tile ends at a black column, and these two are the exception that made
** the waist look like a different picture stuck into the middle of it. */
static void plaque_flanks (unsigned char y)
{
    blit_ch = CH_SOLID;
    blit_cl = CBYTE (0, 0);
    blit_ptr = rowtab[y] + 19;  blit_run (1);
    blit_ptr = rowtab[y] + 29;  blit_run (1);
}

/* Seven tokens, three states, no words.  BASIC 7780.
** Waiting packs left as hollow red rings, home packs right as filled discs
** in the player's own colour, and a piece out on the board leaves its cell
** dark - so the match reads as one horizontal migration. */
static void plaque_tokens (unsigned char q)
{
    unsigned char  w = count_in_pool (q);
    unsigned char  h = count_at_home (q);
    unsigned char  j, lz;
    /* The token row sits on the band's second base row, the far side of
    ** the plaque from the bridge. */
    unsigned char  y = (unsigned char)(band_y[q * 2] + (q ? 1 : 2));
    unsigned char* r;
    unsigned char* c;

    plaque_clear (y);
    lz = player_lum[q];
    if (lz < 4) lz = 4;                 /* never darker than legible        */

    /* The stats recede with the rest of the side that is not to move.  A
    ** floor of 2 rather than of 0: dimmed is not the same as gone, and a
    ** player still has to be able to count the other side's pieces. */
    if (player_dim[q]) {
        lz = (unsigned char)(lz > DIM_DROP ? lz - DIM_DROP : 2);
        if (lz < 2) lz = 2;
    }

    r = rowtab[y] + 21;
    c = r - SCREEN_COLOR_D;
    for (j = 0; j < 7; ++j) {
        if (j < w) { r[j] = CH_RING;
                     c[j] = CBYTE (player_dim[q] ? 2 : 4, 2); }
        if (j > (unsigned char)(6 - h)) {
            r[j] = CH_DISC;
            c[j] = CBYTE (lz, (unsigned char)(player_hue[q] - 1));
        }
    }
}

/* Both plaques whole - BASIC 7700.  In the frozen edition this cost about
** four seconds, which is why it ran once per match and 7650 existed to
** repaint only the token row after a move.  Here it is under a
** millisecond, and the split is kept only because it still expresses who
** owns what. */
void paint_plaques (void)
{
    unsigned char q, r;

    for (q = 0; q < 2; ++q) {
        /* Offsets INTO the player's own band, mirrored so both plaques
        ** read outwards from the bridge.  They were absolute row numbers,
        ** which was correct for a twelve-row board and silently wrong the
        ** moment the black separators pushed the lower band down.
        **
        ** The outer row used to carry the game's own name - "ur royal" over
        ** one plaque, "finkel" under the other.  It is gone during play:
        ** the cabinet says what the game is on the way in, and once the
        ** board is up that row is better spent on black.  A title is for
        ** somebody deciding whether to play; it is noise to somebody
        ** already playing. */
        plaque_clear   ((unsigned char)(band_y[q * 2] + (q ? 3 : 0)));
        plaque_engrave ((unsigned char)(band_y[q * 2] + (q ? 2 : 1)),
                        player_name[q], 6, q);
        plaque_clear   ((unsigned char)(band_y[q * 2] + (q ? 0 : 3)));
        plaque_tokens  (q);
        for (r = 0; r < 4; ++r)
            plaque_flanks ((unsigned char)(band_y[q * 2] + r));
    }
}

/* The per-move update - BASIC 7650. */
void paint_tokens (void)
{
    plaque_tokens (0);
    plaque_tokens (1);
}

/* The chronicle's four unheaded lines at the foot - BASIC 8050. */
static void chronicle_frame (void)
{
    unsigned char  i;
    unsigned char* r;

    for (i = 21; i <= 24; ++i) {
        blit_ptr = rowtab[i];
        blit_ch  = CH_SPACE;
        blit_cl  = 0;
        blit_run (SCR_W - 1);
        r  = rowtab[i];
        *r = CH_RULE;
        *(r - SCREEN_COLOR_D) = CBYTE (5, 7);
    }
}

/* The full board draw - BASIC 1000. */
void board_draw (void)
{
    unsigned char q, j;

    /* BASIC 1005: `COLOR 0,1,0` - true black behind the playfield.  It is
    ** load-bearing, not cosmetic: every engraved element in this game is
    ** reverse video, and reverse video renders its glyph in the BACKGROUND
    ** colour.  Against a tinted background the plaque lettering and the
    ** rosette stars come out tinted; against black they come out black,
    ** which is what an engraved plate should look like. */
    TED_BGCOLOR = CBYTE (0, 0);

    screen_fill (CH_SPACE, 0);
    paint_table ();
    paint_buttons ();

    for (q = 0; q < 2; ++q)                     /* BASIC 1040-1050 */
        for (j = 1; j <= PIECES; ++j)
            if (piece[q][j] > SQ_POOL && piece[q][j] < SQ_HOME)
                draw_cell (q, piece[q][j]);

    paint_plaques ();
    chronicle_frame ();

    /* The board is now the thing on screen, so the light may start
    ** crossing it.  screen_fill above switched it off; this switches it
    ** back on, which is also what resets the sweep to the left edge. */
    shimmer_enable (1);
}

/* Repaint everything whose colour depends on whose turn it is: the twenty
** squares, whatever stands on them, and both waist plaques.  Called when
** the turn changes rather than continuously, because the answer only
** changes then - and it is under three milliseconds, which is the whole
** reason a board can afford to be relit at all.  In the frozen edition
** this would have been most of a minute. */
void board_relight (void)
{
    unsigned char q, j;

    paint_buttons ();
    for (q = 0; q < 2; ++q)
        for (j = 1; j <= PIECES; ++j)
            if (piece[q][j] > SQ_POOL && piece[q][j] < SQ_HOME)
                draw_cell (q, piece[q][j]);
    paint_plaques ();
}

/* --- animation -------------------------------------------------------- */

/* The piece glyph sits in the left cell of its square's two-column well,
** on the first of the two base rows.  BASIC 5110 pokes exactly here. */
unsigned char piece_cell_x (unsigned char player, unsigned char square)
{
    return (unsigned char)(sq_x[player][square] + 1);
}

unsigned char piece_cell_y (unsigned char player, unsigned char square)
{
    return (unsigned char)(sq_y[player][square] + 1);
}

/* Time spent on purpose, counted against the RASTER rather than a
** calibrated FOR loop, so a delay means the same number of milliseconds
** whatever the code around it costs.  The BASIC edition had no way to do
** either, which is why its animation speeds were a property of how much
** work each frame happened to be rather than a decision.
**
** This one is bare: it waits and does nothing else.  It is what the
** innermost animation loop (glide) uses. */
void wait_frames (unsigned char n)
{
    unsigned char i;

    for (i = 0; i < n; ++i) {
        while (TED_RASTER_LO != 250) ;
        while (TED_RASTER_LO == 250) ;
    }
}

/* The same wait, with the music sequencer serviced once a frame, so a bed
** does not stop breathing for the second and a half of a dice throw.
**
** WHY THIS IS A SEPARATE FUNCTION AND NOT A LINE INSIDE wait_frames.
** Folding the music service into wait_frames itself - so that every delay
** anywhere in the program kept the bed alive, including the one inside
** glide's per-cell loop - was tried, and the URBOT demo wedged: the whole
** machine stopped, with no screen write of any kind advancing, several
** minutes into a match.  Instrumenting each spin separately showed the CPU
** was not in either raster loop and not in the sequencer, which points at
** the interrupt path rather than at any C loop here.  The trigger is
** timing: the same source built with three extra debug statements in it
** ran to a finish.
**
** So the discipline the program had before, and now has again, is that the
** sequencer is serviced from SHALLOW call sites - a controller wait, a
** throw, a curtain step - and never from inside the innermost drawing
** loop.  That is the arrangement every long run in this project has been
** stable under.  The wedge itself is not understood and is recorded as
** such in the backlog; what is known is which side of the line is safe. */
void wait_frames_live (unsigned char n)
{
    unsigned char i;

    for (i = 0; i < n; ++i) {
        while (TED_RASTER_LO != 250) ;
        while (TED_RASTER_LO == 250) ;
        music_service ();
        shimmer_frame ();
        /* The keyboard is scanned HERE and nowhere else, which is what
        ** makes its debounce a length of time rather than a number of
        ** calls - see the note over kbd_scan.  It belongs with the music
        ** for the same reason the music belongs here: this is the shallow
        ** wait every part of the game passes through, so putting it here
        ** gives one rate, 50 Hz, everywhere. */
        kbd_scan ();
    }
}

/* Motion tweening.  The BASIC edition moved a piece one whole board square
** per step because a square is 32 POKEs and a POKE was 23 ms; at 2 ms a
** square there is room to move one CHARACTER cell at a time instead, so
** the piece travels rather than teleports.
**
** The glyph is drawn over whatever is underneath, and the cell it covered
** is put back before the next step - the same "restore what you disturbed"
** discipline as the BASIC renderer, one order of magnitude finer. */
void glide (unsigned char x0, unsigned char y0,
            unsigned char x1, unsigned char y1,
            unsigned char ch, unsigned char cl, unsigned char pace)
{
    signed char   dx = (signed char)(x1 - x0);
    signed char   dy = (signed char)(y1 - y0);
    unsigned char steps, i;
    unsigned char sx, sy, keep_ch, keep_cl;
    unsigned char adx = (unsigned char)(dx < 0 ? -dx : dx);
    unsigned char ady = (unsigned char)(dy < 0 ? -dy : dy);
    unsigned char* r;

    steps = adx > ady ? adx : ady;
    if (steps == 0) return;

    DBG_BOUND (DBG_BRD, "glx", x1, SCR_W);
    DBG_BOUND (DBG_BRD, "gly", y1, SCR_H);
    DBG_BOUND (DBG_BRD, "gl0x", x0, SCR_W);
    DBG_BOUND (DBG_BRD, "gl0y", y0, SCR_H);
    for (i = 1; i <= steps; ++i) {
        /* Integer interpolation, rounded - a straight line between the two
        ** cells rather than an L-shaped hop. */
        sx = (unsigned char)(x0 + (signed char)(((signed int)dx * i + (dx < 0 ? -(signed int)steps/2 : (signed int)steps/2)) / steps));
        sy = (unsigned char)(y0 + (signed char)(((signed int)dy * i + (dy < 0 ? -(signed int)steps/2 : (signed int)steps/2)) / steps));

        r       = rowtab[sy] + sx;
        keep_ch = *r;
        keep_cl = *(r - SCREEN_COLOR_D);

        *r                    = ch;
        *(r - SCREEN_COLOR_D) = cl;

        wait_frames (pace);

        if (i < steps) {                /* the last cell is the destination */
            *r                    = keep_ch;
            *(r - SCREEN_COLOR_D) = keep_cl;
        }
    }
}
