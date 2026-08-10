/* ------------------------------------------------------------------------
 * etch.c - the laser.  See etch.h for what was taken from
 * terminaltexteffects' `laseretch` and what was not.
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "board.h"
#include "text.h"
#include "etch.h"

extern unsigned char scr_code (unsigned char c);

/* --- the palette ------------------------------------------------------
**
** The cooling ramp, hottest first.  The original walks an RGB gradient
** from a spark yellow down to the character's final colour; here the last
** entry is the final colour and everything before it is the metal still
** glowing, which on this machine is the same hue at falling luminance
** with white at the front of it.
**
** Seven steps, because eight luminances minus the one it rests at is what
** there is.  A step lasts COOL_HOLD frames, so a character is visibly hot
** for about half a second after the beam has moved on - which is the whole
** reason the effect reads as cutting rather than typing. */
#define COOL_STEPS      7
#define COOL_HOLD       2

static const unsigned char cool[COOL_STEPS] = {
    CBYTE (7, 1),                       /* white hot                      */
    CBYTE (7, 7),                       /* yellow                         */
    CBYTE (6, 8),                       /* orange                         */
    CBYTE (5, 8),
    CBYTE (4, 2),                       /* down into the red              */
    CBYTE (3, 2),
    CBYTE (2, 2),
};

/* The beam itself: a diagonal of these, cycling, so it flickers along its
** length instead of being a static line. */
#define BEAM_LEN        6
static const unsigned char beam_col[4] = {
    CBYTE (7, 1), CBYTE (7, 7), CBYTE (6, 7), CBYTE (5, 8),
};

#define CH_SLASH        47              /* '/' - the beam's own glyph     */
#define CH_SPARK        46              /* '.' - and what it throws off   */
#define CH_HOT          94              /* the up-arrow: a cell mid-cut   */

/* --- what is underneath ----------------------------------------------
**
** The beam and the sparks are drawn over whatever the screen already had
** and taken off again, so every cell they touch is saved before it is
** written.  Same discipline as the piece glide: this owns nothing on the
** screen, it only borrows.  Without it the beam would eat the board it
** passes over, and the beam passes over the board constantly - it rises
** up and to the right from every cut. */
/* Big enough for the longest thing that borrows at once: the beam and its
** sparks need ten, and three firework shells blooming together need ten
** particles each AND a trailing cell behind each of them - twenty a shell,
** sixty at the peak.  Stated as a maximum rather than as a sum, because
** the beam and the shells never run at the same time.
**
** Two hundred and forty bytes of .bss, which on this machine is a real
** price - see the note over ETCH_MAX.  It buys the difference between a
** burst that is ten dots and one that is a shape. */
#define BORROW_MAX      60
static unsigned char save_ch[BORROW_MAX];
static unsigned char save_cl[BORROW_MAX];
static unsigned char save_x[BORROW_MAX];
static unsigned char save_y[BORROW_MAX];
static unsigned char save_n;

static void borrow (unsigned char x, unsigned char y,
                    unsigned char ch, unsigned char cl)
{
    unsigned char* r;

    if (x >= SCR_W || y >= SCR_H) return;
    if (save_n >= BORROW_MAX) return;
    r = rowtab[y] + x;
    save_x[save_n]  = x;
    save_y[save_n]  = y;
    save_ch[save_n] = *r;
    save_cl[save_n] = *(r - SCREEN_COLOR_D);
    ++save_n;
    *r = ch;
    *(r - SCREEN_COLOR_D) = cl;
}

static void give_back (void)
{
    unsigned char* r;

    while (save_n) {
        --save_n;
        r = rowtab[save_y[save_n]] + save_x[save_n];
        *r = save_ch[save_n];
        *(r - SCREEN_COLOR_D) = save_cl[save_n];
    }
}

/* The beam, rising up and to the right from the cut.  Runs off the top of
** the screen rather than stopping short, because a laser comes from
** somewhere off stage and a beam with a visible end is a stick. */
static void beam_draw (unsigned char x, unsigned char y, unsigned char phase)
{
    unsigned char i, cx, cy;

    for (i = 0; i < BEAM_LEN; ++i) {
        if (y < i + 1) break;
        cy = (unsigned char)(y - i - 1);
        cx = (unsigned char)(x + i + 1);
        if (cx >= SCR_W) break;
        borrow (cx, cy, CH_SLASH, beam_col[(i + phase) & 3]);
    }
}

/* Two sparks falling away from the cut.  The original emits from a pool of
** two thousand with eased bezier paths; two is what a forty column screen
** can show without the word disappearing behind its own sparks. */
static void sparks_draw (unsigned char x, unsigned char y, unsigned char t)
{
    unsigned char dy = (unsigned char)(t & 3);

    if (x > 0)          borrow ((unsigned char)(x - 1),
                                (unsigned char)(y + dy + 1),
                                CH_SPARK, cool[dy < COOL_STEPS ? dy : 0]);
    if (x + 1 < SCR_W)  borrow ((unsigned char)(x + 1),
                                (unsigned char)(y + (dy >> 1) + 1),
                                CH_SPARK, cool[1]);
}

/* --- the cut ---------------------------------------------------------- */

/* Up to this many cells in one cut.
**
** Sized for the largest thing that is ever cut, which is a word in BLOCK
** LETTERS: eight of them, and the widest glyph in the font lights
** fourteen of its twenty cells, so a hundred and twelve is the worst case
** exactly rather than a round number above it.
**
** THIS IS .BSS ON A MACHINE WITH NONE TO SPARE.  The link fails - not
** warns, fails - if the program and its data pass $f500, and 27 KB of the
** 30 is transcribed music sized to fill whatever was left (see MIDBUDGET
** in the Makefile). So a byte a cell is a decision here, which is why two
** of the four tables that a cell might want are not there:
**
**   cell_ch  a table of GLYPHS is only wanted by etch_text, and a row of
**            text is forty cells. Block letters and the large UR are
**            solid blocks throughout - `cell_solid` says so, and saves
**            seventy-two bytes of table holding one repeated value.
**   the burn's ignition delay was a fifth table until it was noticed
**            that it is a function of where the cell IS - see burn_apron -
**            and a function is cheaper than a hundred and twelve bytes. */
#define ETCH_MAX        112
/* A cut that carries its own glyphs is one row of ordinary text, and the
** longest any caller asks for is a mode name - `player vs urbot`, fifteen.
** Longer strings are clipped in etch_text rather than overrunning this. */
#define ETCH_CHS        24

static unsigned char cell_x[ETCH_MAX];
static unsigned char cell_y[ETCH_MAX];
static unsigned char cell_st[ETCH_MAX];         /* cooling step, 0 = unlit */
static unsigned char cell_ch[ETCH_CHS];
static unsigned char cell_solid;                /* 1: no glyph table       */
static unsigned char cell_n;

static unsigned char cell_glyph (unsigned char i)
{
    return cell_solid ? CH_SOLID : cell_ch[i];
}

static unsigned char final_col;

/* One frame: advance every cell that is still cooling, then put the beam
** and its sparks over the top.  Cooling continues after the beam has gone,
** which is the point - the word glows down to rest a letter at a time in
** the order it was cut. */
static void etch_frame (unsigned char cut, unsigned char t, unsigned char hold)
{
    unsigned char i, s;
    unsigned char* r;

    for (i = 0; i < cell_n; ++i) {
        if (cell_st[i] == 0) continue;          /* not cut yet             */
        s = (unsigned char)(cell_st[i] - 1);
        if (s >= COOL_STEPS) continue;          /* at rest                 */
        r = rowtab[cell_y[i]] + cell_x[i];
        *r = cell_glyph (i);
        *(r - SCREEN_COLOR_D) = (s == COOL_STEPS - 1) ? final_col : cool[s];
        if ((hold & (COOL_HOLD - 1)) == 0) ++cell_st[i];
    }

    if (cut < cell_n) {
        r = rowtab[cell_y[cut]] + cell_x[cut];
        *r = CH_HOT;
        *(r - SCREEN_COLOR_D) = cool[0];
        beam_draw (cell_x[cut], cell_y[cut], t);
        sparks_draw (cell_x[cut], cell_y[cut], t);
    }
}

/* Run the cut to completion in about `frames` frames.
**
** THE PACE IS DERIVED, NOT SET.  The caller owns the clock - two seconds
** for a capture, three for a win - so the rate falls out of how many cells
** there are and how long there is.  A long word is cut faster, not for
** longer.  That inversion is the difference between a screensaver and a
** marquee.
**
** IT TAKES BOTH OF THE ORIGINAL'S KNOBS, and the first version took only
** one, which is why it looked like a print rather than a cut.  The
** original has etch_speed - cells per group - AND etch_delay - frames
** between groups.  Implementing speed alone means a group every frame, so
** eight letters over an eighty-six frame budget were cut in eight frames
** and the effect then sat still for the remaining seventy-eight.  It was
** finishing in a sixth of a second and calling it two seconds.
**
** So the two are derived together, and which one moves depends on which
** side of the budget the cell count falls:
**
**    fewer cells than frames   one cell per group, and WAIT between them
**    more cells than frames    a group every frame, and cut SEVERAL
**
** A word gets the first, the large monogram at a long budget gets the
** first as well, and only something bigger than its budget gets the
** second. */
static void run (unsigned char frames)
{
    unsigned char t, cut = 0, per, hold = 0, delay, wait = 0;
    unsigned int  budget;

    if (!cell_n) return;

    /* Leave room at the end for the last letter to finish cooling, or the
    ** word snaps to its final colour the instant the beam stops. */
    budget = (unsigned int)frames;
    if (budget > COOL_STEPS * COOL_HOLD)
        budget -= COOL_STEPS * COOL_HOLD;
    else
        budget = 1;

    if ((unsigned int)cell_n <= budget) {
        per   = 1;
        delay = (unsigned char)(budget / cell_n);
        if (!delay) delay = 1;
    } else {
        per   = (unsigned char)((cell_n + budget - 1) / budget);
        delay = 1;
    }

    for (t = 0; t < frames; ++t) {
        if (cut < cell_n && wait == 0) {
            unsigned char k;
            for (k = 0; k < per && cut < cell_n; ++k) {
                cell_st[cut] = 1;               /* struck: start cooling   */
                ++cut;
            }
            wait = delay;
        }
        if (wait) --wait;
        etch_frame ((unsigned char)(cut ? cut - 1 : 0), t, hold);
        ++hold;
        wait_frames_live (1);
        give_back ();                           /* the beam was borrowed   */
    }

    /* Everything at rest, whatever the arithmetic did. */
    for (t = 0; t < cell_n; ++t) {
        unsigned char* r = rowtab[cell_y[t]] + cell_x[t];
        *r = cell_glyph (t);
        *(r - SCREEN_COLOR_D) = final_col;
    }
}

void etch_text (const char* s, unsigned char y, unsigned char frames)
{
    unsigned char n = 0, i, x0;

    /* One row, so a word longer than the row is clipped rather than
    ** written off the end of it - which is also what keeps cell_n inside
    ** the glyph table, the one table still sized per character. */
    while (s[n] && n < ETCH_CHS) ++n;
    if (!n) return;
    x0 = (unsigned char)(n >= SCR_W ? 0 : (SCR_W - n) >> 1);

    cell_n = 0;
    for (i = 0; i < n; ++i) {
        if (s[i] == ' ') continue;              /* nothing to cut          */
        cell_x[cell_n]  = (unsigned char)(x0 + i);
        cell_y[cell_n]  = y;
        cell_ch[cell_n] = scr_code ((unsigned char)s[i]);
        cell_st[cell_n] = 0;
        ++cell_n;
    }
    cell_solid = 0;                             /* it carries its own glyphs */
    final_col  = CBYTE (7, 7);                  /* gold, at rest           */
    run (frames);
}

/* --- the block font ---------------------------------------------------
**
** Four columns by five rows, one character cell per lit pixel, at a five
** column pitch - so eight letters span thirty-nine of the forty columns
** and a player's name fills the screen exactly.  That number is where the
** whole design comes from: names are eight characters because the input
** field is eight characters, so the largest legible glyph that fits eight
** of them across is four wide, and four wide with one column of air is
** five.  Everything else here follows from that.
**
** The large UR above uses six by seven, because it is two letters and can
** afford to.  This is the font for words. */
#define BLK_W           4
#define BLK_H           5
#define BLK_PITCH       5
#define BLK_MAX         8               /* eight letters is thirty-nine    */

/* Bits 3..0, left to right.  a-z then 0-9. */
static const unsigned char blk_font[36][BLK_H] = {
    { 0x6, 0x9, 0xF, 0x9, 0x9 },        /* a */
    { 0xE, 0x9, 0xE, 0x9, 0xE },        /* b */
    { 0x7, 0x8, 0x8, 0x8, 0x7 },        /* c */
    { 0xE, 0x9, 0x9, 0x9, 0xE },        /* d */
    { 0xF, 0x8, 0xE, 0x8, 0xF },        /* e */
    { 0xF, 0x8, 0xE, 0x8, 0x8 },        /* f */
    { 0x7, 0x8, 0xB, 0x9, 0x7 },        /* g */
    { 0x9, 0x9, 0xF, 0x9, 0x9 },        /* h */
    { 0xF, 0x6, 0x6, 0x6, 0xF },        /* i */
    { 0x3, 0x1, 0x1, 0x9, 0x6 },        /* j */
    { 0x9, 0xA, 0xC, 0xA, 0x9 },        /* k */
    { 0x8, 0x8, 0x8, 0x8, 0xF },        /* l */
    { 0x9, 0xF, 0xF, 0x9, 0x9 },        /* m */
    { 0x9, 0xD, 0xB, 0x9, 0x9 },        /* n */
    { 0x6, 0x9, 0x9, 0x9, 0x6 },        /* o */
    { 0xE, 0x9, 0xE, 0x8, 0x8 },        /* p */
    { 0x6, 0x9, 0x9, 0xA, 0x5 },        /* q */
    { 0xE, 0x9, 0xE, 0xA, 0x9 },        /* r */
    { 0x7, 0x8, 0x6, 0x1, 0xE },        /* s */
    { 0xF, 0x6, 0x6, 0x6, 0x6 },        /* t */
    { 0x9, 0x9, 0x9, 0x9, 0x6 },        /* u */
    { 0x9, 0x9, 0x9, 0x6, 0x6 },        /* v */
    { 0x9, 0x9, 0xF, 0xF, 0x9 },        /* w */
    { 0x9, 0x6, 0x6, 0x6, 0x9 },        /* x */
    { 0x9, 0x9, 0x6, 0x6, 0x6 },        /* y */
    { 0xF, 0x1, 0x6, 0x8, 0xF },        /* z */
    { 0x6, 0x9, 0x9, 0x9, 0x6 },        /* 0 */
    { 0x4, 0xC, 0x4, 0x4, 0xF },        /* 1 */
    { 0xE, 0x1, 0x6, 0x8, 0xF },        /* 2 */
    { 0xE, 0x1, 0x6, 0x1, 0xE },        /* 3 */
    { 0x9, 0x9, 0xF, 0x1, 0x1 },        /* 4 */
    { 0xF, 0x8, 0xE, 0x1, 0xE },        /* 5 */
    { 0x6, 0x8, 0xE, 0x9, 0x6 },        /* 6 */
    { 0xF, 0x1, 0x2, 0x4, 0x4 },        /* 7 */
    { 0x6, 0x9, 0x6, 0x9, 0x6 },        /* 8 */
    { 0x6, 0x9, 0x7, 0x1, 0x6 },        /* 9 */
};

static unsigned char blk_index (char c)
{
    if (c >= 'a' && c <= 'z') return (unsigned char)(c - 'a');
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c - 'A');
    if (c >= '0' && c <= '9') return (unsigned char)(26 + (c - '0'));
    return 255;                         /* takes its slot, stays blank     */
}

/* Lay a word out as cells, and DO IT BEFORE ANY FRAME IS WAITED.
**
** Not a style point: on this toolchain a pointer argument is reloaded
** through `(sp),y` at each use and does not survive an animation - the
** trophy engraving was silently doing nothing for exactly this reason
** (see the note in trophy_draw).  Every caller below reads its string
** here, in full, at the top, and never touches it again. */
static void word_cells (const char* s, unsigned char y0)
{
    unsigned char n = 0, i, row, col, g, x0, bits, cx;

    while (s[n] && n < BLK_MAX) ++n;
    cell_n     = 0;
    cell_solid = 1;                     /* every lit pixel is a full cell  */
    if (!n) return;

    x0 = (unsigned char)((SCR_W - (n * BLK_PITCH - 1)) >> 1);
    for (i = 0; i < n; ++i) {
        g = blk_index (s[i]);
        if (g == 255) continue;
        cx = (unsigned char)(x0 + i * BLK_PITCH);
        for (row = 0; row < BLK_H; ++row) {
            bits = blk_font[g][row];
            for (col = 0; col < BLK_W; ++col) {
                if (!(bits & (unsigned char)(1 << (BLK_W - 1 - col)))) continue;
                if (cell_n >= ETCH_MAX) return;
                cell_x[cell_n]  = (unsigned char)(cx + col);
                cell_y[cell_n]  = (unsigned char)(y0 + row);
                cell_st[cell_n] = 0;
                ++cell_n;
            }
        }
    }
}

void etch_word (const char* s, unsigned char y, unsigned char frames)
{
    word_cells (s, y);
    if (!cell_n) return;
    final_col = CBYTE (7, 7);                   /* gold, at rest           */
    run (frames);
}

/* --- the two letters, large ------------------------------------------
**
** Six columns by seven rows each, as bitmaps, because a large letter on a
** character screen is a bitmap whether or not it is called one.  They are
** cut in the same reading order as text - left to right, top to bottom -
** so the beam travels the way an eye does. */
#define BIG_W   6
#define BIG_H   7

static const unsigned char big_u[BIG_H] = {
    0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x1E,
};
static const unsigned char big_r[BIG_H] = {
    0x3E, 0x21, 0x21, 0x3E, 0x24, 0x22, 0x21,
};

/* THE TITLE, in two cuts.
**
** Nine letters at six columns each would be fifty-four columns wide and
** the screen is forty, so the large treatment goes to UR - the part worth
** enlarging - and FINKEL is cut beneath it at ordinary size.  The budget
** is split two to one in favour of the large letters, because they are
** thirty-six cells against six and cutting them at the same rate would
** have the word finished long before the monogram. */
void etch_title (unsigned char frames)
{
    unsigned char big = (unsigned char)((frames * 2U) / 3U);

    etch_big_ur (big);
    etch_text ("finkel", 17, (unsigned char)(frames - big));
}

void etch_big_ur (unsigned char frames)
{
    unsigned char row, col, x0, y0;

    /* Two letters and a gap: thirteen columns, centred; seven rows placed
    ** so the pair sits in the upper half of the stage. */
    x0 = (unsigned char)((SCR_W - (BIG_W * 2 + 1)) / 2);
    y0 = 9;

    cell_n     = 0;
    cell_solid = 1;
    for (row = 0; row < BIG_H; ++row) {
        for (col = 0; col < BIG_W; ++col) {
            if (big_u[row] & (unsigned char)(1 << (BIG_W - 1 - col))) {
                if (cell_n < ETCH_MAX) {
                    cell_x[cell_n]  = (unsigned char)(x0 + col);
                    cell_y[cell_n]  = (unsigned char)(y0 + row);
                    cell_st[cell_n] = 0;
                    ++cell_n;
                }
            }
            if (big_r[row] & (unsigned char)(1 << (BIG_W - 1 - col))) {
                if (cell_n < ETCH_MAX) {
                    cell_x[cell_n]  = (unsigned char)(x0 + BIG_W + 1 + col);
                    cell_y[cell_n]  = (unsigned char)(y0 + row);
                    cell_st[cell_n] = 0;
                    ++cell_n;
                }
            }
        }
    }
    final_col = CBYTE (7, 7);
    run (frames);
}

/* --- fireworks -------------------------------------------------------
**
** Launch, burst, bloom.  See etch.h for what was taken from the original
** and what was left behind.
**
** THE ARITHMETIC IS ALL INTEGER AND ALL TABLE.  The original places
** particles with find_coords_in_circle and moves them along eased bezier
** paths; there is no room for any of that here, and no need - eight
** directions on a small circle, scaled by a radius that grows, is
** indistinguishable from a circle at this resolution.
**
** The two direction tables are not symmetrical, and that is deliberate: a
** character cell is about twice as tall as it is wide, so a burst that
** moved the same number of cells up as sideways would be an ellipse
** standing on end.  The column offsets are the larger ones. */

#define FW_PARTS        10              /* particles in a shell           */
#define FW_RISE         14              /* frames from the floor to apex  */
#define FW_BLOOM        26              /* frames of burst and fall       */
#define FW_LIFE         (FW_RISE + FW_BLOOM)
#define FW_LIVE         3               /* shells in the air at once      */
#define FW_GAP          11              /* frames between launches        */
#define FW_SITES        8               /* places one can go off          */

/* Twelve points on a circle, at a thirtieth of a turn each, scaled by
** eight.  The column offsets are twice the row offsets for the same
** angle, because a character cell is about twice as tall as it is wide
** and a ring drawn with equal offsets is an ellipse standing on end. */
static const signed char fw_dx[FW_PARTS] = {
     0,  5,  8,  8,  5,  0, -5, -8, -8, -5,
};
static const signed char fw_dy[FW_PARTS] = {
    -4, -3, -1,  1,  3,  4,  3,  1, -1, -3,
};

/* One hue per site, and the luminance falls as the shell ages - which is
** what a firework does and what this machine can express exactly. */
static const unsigned char fw_hue[FW_SITES] = { 7, 11, 3, 5, 8, 13, 2, 10 };

/* Where the shells go off.  Fixed rather than random: this runs over a
** trophy and a name, and a shell that bursts on top of the winner's name
** is a shell in the wrong place.  Eight sites walked in turn spread the
** display across the whole screen without ever putting two consecutive
** bursts in the same place. */
static const unsigned char fw_col[FW_SITES] = { 7, 32, 19, 12, 27, 4, 35, 16 };
static const unsigned char fw_top[FW_SITES] = { 6,  4,  9, 11,  5, 8, 10,  3 };

/* One shell, at age `t`.  Draws only - every cell is borrowed, and the
** caller gives them all back once the frame has been shown. */
static void fw_draw (unsigned char site, unsigned char t)
{
    unsigned char i, hue = fw_hue[site];
    unsigned char cx = fw_col[site], ay = fw_top[site];
    unsigned char r, lum, tb, row;
    signed int    x, y;

    /* THE LAUNCH.  A single cell climbing, fastest at the start and easing
    ** off - out_expo in the original, and here just a rise that slows,
    ** because a shell that climbs at a constant rate looks winched.  The
    ** trailing cell behind it is what makes the climb visible at all at
    ** one cell a frame. */
    if (t < FW_RISE) {
        row = (unsigned char)(APRON_BOT -
                              ((unsigned int)(APRON_BOT - ay) * t) / FW_RISE);
        borrow (cx, row, CH_SPARK, CBYTE (7, 1));
        if (row + 1 <= APRON_BOT)
            borrow (cx, (unsigned char)(row + 1), CH_SPARK, CBYTE (4, 8));
        return;
    }

    /* THE BURST AND THE BLOOM.  The radius grows quickly then holds; the
    ** particles fall away as it does, and the whole thing dims.  Three of
    ** the original's movements folded into one loop, because on this
    ** machine they are all just "where is the cell and what colour". */
    /* THE RING OPENS AT THREE, NOT AT ONE, and that is not a taste
    ** decision - it is integer division.  A particle sits at dx*r/8, so at
    ** r=1 every one of the ten offsets divides to nought or one and the
    ** whole burst lands on three cells in a single row.  The first version
    ** grew from one and spent its four brightest frames looking like a
    ** full stop.  Opening at three puts a readable ring on the screen in
    ** the frame the shell bursts, which is the frame the eye is on.
    **
    ** Eight is where it stops.  Twelve was tried and made a burst of loose
    ** dots: ten particles round a twenty-four column ellipse are five
    ** cells apart and the eye joins nothing.
    **
    ** And the dimming is SLOWER THAN IT LOOKS LIKE IT SHOULD BE, for a
    ** reason peculiar to this machine: luminance 1 on black is all but
    ** invisible, so a ramp that ends there ends by deleting the shell
    ** rather than fading it.  It stops at 3, which still reads as an
    ** ember, and takes six frames a step to get there. */
    tb  = (unsigned char)(t - FW_RISE);
    r   = (unsigned char)(tb < 6 ? tb + 3 : 8);
    lum = (unsigned char)(7 - tb / 6);
    if (lum < 3) lum = 3;

    /* EACH PARTICLE IS DRAWN TWICE, at the radius it has reached and at
    ** three behind it - one bright, one two luminances down.
    **
    ** This is a trail, and it is drawn rather than remembered on purpose.
    ** Letting the cells stand for three frames before giving them back
    ** would leave a longer one for the same total work, but the whole
    ** trail would then have to be restored in one go every third frame -
    ** the borrow stack has to unwind in the order it was filled or a cell
    ** two particles landed on is put back wrong - and a trail that
    ** vanishes and reappears sixteen times a second is a flicker, not a
    ** trail.
    **
    ** So the second cell is simply where the particle WAS, computed the
    ** same way as where it is.  A still frame then carries the shape of
    ** the burst instead of ten loose dots, which matters here more than
    ** usual: ten cells lit at once on a forty column screen is not a
    ** firework, and two rings of ten is. */
    for (i = 0; i < FW_PARTS; ++i) {
        x = (signed int)cx + ((signed int)fw_dx[i] * r) / 8;
        /* Gravity: the fall accelerates, so the ring becomes a teardrop
        ** the way a real burst does. */
        y = (signed int)ay + ((signed int)fw_dy[i] * r) / 8
            + (signed int)(((unsigned int)tb * tb) / 40);
        if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H)
            borrow ((unsigned char)x, (unsigned char)y,
                    (unsigned char)(tb < 12 ? CH_STAR : CH_SPARK),
                    CBYTE (lum, hue));

        if (r < 6) continue;            /* nothing behind it yet           */
        x = (signed int)cx + ((signed int)fw_dx[i] * (r - 3)) / 8;
        y = (signed int)ay + ((signed int)fw_dy[i] * (r - 3)) / 8
            + (signed int)(((unsigned int)tb * tb) / 40);
        if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H) continue;
        borrow ((unsigned char)x, (unsigned char)y, CH_SPARK,
                CBYTE ((unsigned char)(lum > 4 ? lum - 2 : 2), hue));
    }
}

void fireworks (unsigned char frames)
{
    unsigned char t, k, gap = 0, site = 0, flash = 0, keep;
    unsigned char sh_t[FW_LIVE];        /* age, or 255 for an empty slot  */
    unsigned char sh_s[FW_LIVE];        /* which site it is going off at  */

    /* Masked, because only seven of $ff19's bits are the border colour and
    ** what the eighth reads back as is not this program's business. */
    keep = (unsigned char)(TED_BORDER & 0x7F);
    for (k = 0; k < FW_LIVE; ++k) sh_t[k] = 255;

    for (t = 0; t < frames; ++t) {
        /* Launch, unless there is no longer room for a whole shell inside
        ** the budget - the display has to be OVER when the caller's time
        ** is up, not cut off mid-burst. */
        if (gap == 0 && (unsigned int)t + FW_LIFE <= (unsigned int)frames) {
            for (k = 0; k < FW_LIVE; ++k) {
                if (sh_t[k] != 255) continue;
                sh_t[k] = 0;
                sh_s[k] = site;
                site    = (unsigned char)((site + 1) & (FW_SITES - 1));
                gap     = FW_GAP;
                break;
            }
        }
        if (gap) --gap;

        for (k = 0; k < FW_LIVE; ++k) {
            if (sh_t[k] == 255) continue;
            /* THE ROOM LIGHTS UP.  A forty column screen cannot show the
            ** glow of a burst on everything around it; the border can. */
            if (sh_t[k] == FW_RISE) {
                TED_BORDER = CBYTE (7, fw_hue[sh_s[k]]);
                flash = 2;
            }
            fw_draw (sh_s[k], sh_t[k]);
            if (++sh_t[k] >= FW_LIFE) sh_t[k] = 255;
        }
        if (flash && --flash == 0) TED_BORDER = keep;

        wait_frames_live (1);
        give_back ();
    }
    TED_BORDER = keep;
}

/* --- burn -------------------------------------------------------------
**
** See etch.h for what this takes from the original and what it drops.
**
** THE MORPH TABLE is the effect.  A burning cell is not the final
** character in a hot colour - it is a lump of flame that only becomes the
** character once it has cooled, and the glyph changing is what says so.
** The original's sequence is ' . ▖ ▙ █ ▜ ▀ ▝ . ; these are the nearest
** shapes this character set has, and they follow the same arc: a speck, a
** thickening, a full block at the peak, then breaking up and going out.
**
** The last step is the character itself, at rest.  The colour walks a fire
** gradient over the top - dull red up through orange and yellow to white
** at the peak, then back down - which on eight luminances is more range
** than the shape has. */
#define BURN_STEPS      9
#define BURN_HOLD       3               /* frames per step of the morph   */

static const unsigned char burn_ch[BURN_STEPS] = {
    39,                                 /* '   a speck catching           */
    46,                                 /* .                              */
    CH_HATCH,                           /*     thickening                 */
    CH_SOLID,                           /*     alight                     */
    CH_SOLID,                           /*     the peak                   */
    CH_HATCH,                           /*     breaking up                */
    46,                                 /* .                              */
    39,                                 /* '   going out                  */
    0,                                  /*     the character, at rest     */
};

static const unsigned char burn_cl[BURN_STEPS] = {
    CBYTE (2, 2), CBYTE (4, 2), CBYTE (6, 8), CBYTE (7, 7), CBYTE (7, 1),
    CBYTE (7, 7), CBYTE (6, 8), CBYTE (4, 2), CBYTE (6, 8),
};

/* --- the fire bed -----------------------------------------------------
**
** A height per column along the bottom of the apron - AND IT IS NOT STORED
** ANYWHERE.  The height of column x at frame t is a function of the two of
** them and the envelope, so the previous frame's height is not a fact to
** be remembered, it is the same function called with t-1.  That is forty
** bytes of .bss the fire does not cost, on a machine where the link fails
** over one.
**
** What a cell looks like depends on WHICH ROW IT IS IN and not on how far
** it is from the tip of its own column, which is the decision that makes
** this affordable - a column whose height changes by one costs one cell
** written, not a repaint of the column.  At forty columns a frame that is
** the difference between a fire and a slideshow.
**
** It is also true to the thing: the base of a fire is white, the middle
** is yellow and orange, and the top is red and breaking up, wherever the
** individual flames happen to reach. */
#define FL_MAX          6               /* rows 24 up to 19               */

static const unsigned char fl_ch[FL_MAX] = {
    CH_SOLID, CH_SOLID, CH_HATCH, CH_HATCH, 46, 39,
};
static const unsigned char fl_cl[FL_MAX] = {
    CBYTE (7, 1), CBYTE (7, 7), CBYTE (6, 8),
    CBYTE (5, 8), CBYTE (4, 2), CBYTE (2, 2),
};

/* The ripple.  A fire does not stand still and it does not flicker at
** random either - it leans, and the lean travels along it.  Indexing this
** by `x * 3 + t` makes the pattern move sideways at a third of a column a
** frame while every column is doing something different from its
** neighbour. */
static const unsigned char fl_wob[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };

/* How far below FL_MAX the envelope holds at full blaze - which is how
** many rows of the fire ripple rather than standing solid.  Three of the
** six: the bottom half is a body of flame and the top half is tips. */
#define FL_RIPPLE       3

/* How tall column x stands at the frame whose envelope is `env`.  The
** ripple shrinks with the envelope, so the fire goes out at the end
** instead of flickering at height two for ever. */
static unsigned char flame_h (unsigned char env, unsigned char x,
                              unsigned char t)
{
    unsigned char w = fl_wob[(x * 3 + t) & 7];

    if (env < FL_RIPPLE && w > env) w = env;
    w = (unsigned char)(env + w);
    return w > FL_MAX ? FL_MAX : w;
}

/* Draw the change and nothing else: `old` is what the column is showing,
** `h` is what it should show. */
static void flame_col (unsigned char x, unsigned char old, unsigned char h)
{
    unsigned char d, y;
    unsigned char* r;

    if (h > old) {
        for (d = old; d < h; ++d) {
            y = (unsigned char)(APRON_BOT - d);
            r = rowtab[y] + x;
            *r = fl_ch[d];
            *(r - SCREEN_COLOR_D) = fl_cl[d];
        }
    } else if (h < old) {
        for (d = h; d < old; ++d) {
            y = (unsigned char)(APRON_BOT - d);
            r = rowtab[y] + x;
            *r = CH_SPACE;
            *(r - SCREEN_COLOR_D) = 0;
        }
    }
}

void apron_clear (void)
{
    rows_fill (APRON_TOP, APRON_BOT, CH_SPACE, 0);
    chronicle_redraw ();
}

/* THE STAGE ALIGHT.
**
** Three things run at once for the whole of `frames`, and the reason it
** reads as one event rather than three effects is that they share an
** envelope: the fire builds, blazes and dies, and the word catches on the
** way up and is left standing spent on the way down.
**
**   0        frames/6      frames/5              2*frames/3      frames
**   |  bed    |  the word   |  full blaze         |  dying back    |
**      rises     starts        word burning          word stands
**                catching      through to solid      and cools
**
** The word occupies the top five rows of the apron and the flames the
** bottom six, so nothing has to be drawn in an order that matters.  That
** is a layout decision doing the work an ownership rule would otherwise
** have to do. */
void burn_apron (const char* s, unsigned char frames)
{
    unsigned char t, i, x, st, env, was = 0, mhold = 0, adv, bot;
    unsigned char ign, fl_up, fl_dn;
    unsigned char* r;

    /* The word is read here and nowhere else - see word_cells. */
    word_cells (s, APRON_TOP);
    if (!cell_n) return;

    /* Too short a budget and the phases below collide; the effect states
    ** its own minimum rather than dividing by zero on the way to it. */
    if (frames < 60) frames = 60;

    bot = (unsigned char)(APRON_TOP + BLK_H - 1);
    for (i = 0; i < cell_n; ++i) cell_st[i] = 0;

    rows_fill (APRON_TOP, APRON_BOT, CH_SPACE, 0);   /* nothing standing */

    fl_up = (unsigned char)(frames / 5);
    ign   = (unsigned char)(frames / 6);
    fl_dn = (unsigned char)(frames - frames / 3);

    for (t = 0; t < frames; ++t) {
        /* The envelope, as a height the columns wobble around.  Held
        ** FL_RIPPLE below FL_MAX so the ripple has somewhere to go up
        ** to. */
        if (t < fl_up)
            env = (unsigned char)(((unsigned int)t * (FL_MAX - FL_RIPPLE)) / fl_up);
        else if (t < fl_dn)
            env = FL_MAX - FL_RIPPLE;
        else {
            unsigned int gone = ((unsigned int)(t - fl_dn) * (FL_MAX - FL_RIPPLE))
                                / (unsigned int)(frames - fl_dn);
            env = (unsigned char)(gone >= FL_MAX - FL_RIPPLE ? 0
                                  : (FL_MAX - FL_RIPPLE) - gone);
        }

        /* `was` is the envelope one frame ago, which with flame_h is the
        ** whole of the previous frame - so the fire needs no memory of
        ** itself.  At t = 0 there is nothing standing to erase. */
        for (x = 0; x < SCR_W; ++x)
            flame_col (x,
                       t ? flame_h (was, x, (unsigned char)(t - 1)) : 0,
                       flame_h (env, x, t));

        adv = (unsigned char)(mhold == 0);
        if (++mhold >= BURN_HOLD) mhold = 0;

        for (i = 0; i < cell_n; ++i) {
            if (cell_st[i] == 0) {
                /* IGNITION CLIMBS, and the delay is a FUNCTION of where the
                ** cell is rather than a table of a hundred and twelve
                ** bytes: how far it stands above the flames, six frames a
                ** row, plus a little per column so the wavefront is ragged
                ** rather than a rising bar.  The bottom row of the letters
                ** is where the fire is, so the bottom row catches first.
                ** Computed only while the cell is still unlit, which is a
                ** shrinking set. */
                if (t < ign) continue;
                if ((unsigned char)(t - ign) <
                    (unsigned char)((bot - cell_y[i]) * 6
                                    + ((cell_x[i] * 3) & 7))) continue;
                cell_st[i] = 1;                 /* caught                  */
            }
            st = (unsigned char)(cell_st[i] - 1);
            if (st >= BURN_STEPS) continue;     /* spent, and standing     */
            r = rowtab[cell_y[i]] + cell_x[i];
            *r = burn_ch[st] ? burn_ch[st] : cell_glyph (i);
            *(r - SCREEN_COLOR_D) = burn_cl[st];
            if (adv) ++cell_st[i];
        }

        wait_frames_live (1);
        was = env;
    }

    /* Whatever the arithmetic did: the fire is out and the word is
    ** standing when this returns. */
    for (x = 0; x < SCR_W; ++x)
        flame_col (x, flame_h (was, x, (unsigned char)(frames - 1)), 0);
    for (i = 0; i < cell_n; ++i) {
        r = rowtab[cell_y[i]] + cell_x[i];
        *r = cell_glyph (i);
        *(r - SCREEN_COLOR_D) = burn_cl[BURN_STEPS - 1];
    }
}
