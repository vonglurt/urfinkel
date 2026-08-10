/* ------------------------------------------------------------------------
 * front.c - the theatre front, the curtains and the trophy.
 *
 * Ported from the frozen edition's 8400-8495, 8500, 8700 and 6200-6285.
 * The colours and the geometry are carried over rather than re-derived,
 * for the same reason the board renderer carries its constants over: the
 * two editions should look like the same cabinet.
 *
 * The one thing that is genuinely new is the travel.  The BASIC curtain
 * slid six columns, because seventeen rows of POKEs per column was already
 * most of a second; here a column is a blitter call, so the curtain closes
 * all the way to the middle of the screen the way a real one does.  That
 * makes the animation LONGER, not shorter, and deliberately so - see the
 * pacing note below.
 *
 * --- on pacing ---------------------------------------------------------
 *
 * Every delay in this file is counted in frames off the raster, never in
 * loop iterations.  On the interpreted edition the machine's own slowness
 * set the pace of everything, and it happened to land in the region a
 * human reads as deliberate.  Compiled, the same code runs a few hundred
 * times faster: a curtain would snap, a trophy would appear rather than be
 * revealed, and the lots would blink instead of tumbling.  So the pace is
 * now stated rather than inherited, and it is stated in the same units the
 * music counts in - a frame is 1/50 s, and the fanfare is 172 of them.
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "board.h"
#include "text.h"
#include "front.h"
#include "music.h"
#include "dbg.h"

/* Stamped by the Makefile; a fallback so the file still compiles alone. */
#ifndef BUILD_DATE
#  define BUILD_DATE "unknown"
#endif

extern unsigned char scr_code (unsigned char c);

/* --- the proscenium --------------------------------------------------- */

/* The woven frieze: solid and half-shaded cells alternating, the hue
** cycling gold-brown-yellow every three columns and the luminance every
** four, so the two never line up and the band reads as woven rather than
** as a repeat.  Rows 1 and 3 run the luminance two columns out of step
** with each other, which is what makes the pair look plaited. */
static void frieze_row (unsigned char y, unsigned char skew)
{
    unsigned char i, ch, hue, lum;
    unsigned char* r = rowtab[y];
    unsigned char* c = r - SCREEN_COLOR_D;

    for (i = 0; i < SCR_W; ++i) {
        ch  = (unsigned char)((i & 1) ? CH_HATCH : CH_SOLID);
        hue = (unsigned char)(i % 3);
        hue = (unsigned char)(hue == 0 ? 7 : (hue == 1 ? 9 : 8));
        lum = (unsigned char)(3 + ((i + skew) & 3));
        r[i] = ch;
        c[i] = CBYTE (lum, hue);
    }
}

/* A lamp band: a dark rail with a bulb every other column.  The bulbs are
** reverse discs, so the chase can be run entirely in colour RAM - the
** screen matrix never changes once the band is drawn. */
static void lamp_band (unsigned char y, unsigned char odd)
{
    unsigned char i;

    blit_ptr = rowtab[y];
    blit_ch  = CH_SOLID;
    blit_cl  = CBYTE (1, 9);
    blit_run (SCR_W);

    for (i = 0; i < 20; ++i)
        rowtab[y][2 * i + odd] = CH_DISC | CH_REVERSE;
}

void lamps_frame (unsigned char phase)
{
    unsigned char i, k, c;
    unsigned char* c0  = rowtab[0]  - SCREEN_COLOR_D;
    unsigned char* c4  = rowtab[4]  - SCREEN_COLOR_D;
    unsigned char* c24 = rowtab[24] - SCREEN_COLOR_D;

    for (i = 0; i < 20; ++i) {
        /* Two lit of every five, which is sparse enough that the eye
        ** follows the gap rather than the bulbs. */
        k = (unsigned char)((i + 20 - (phase % 5)) % 5);
        c = (unsigned char)(k < 2 ? CBYTE (7, 7) : CBYTE (1, 9));
        c0[2 * i]  = c;
        c24[2 * i] = c;

        /* The middle band runs the other way, so the marquee reads as one
        ** current going round rather than three going the same way. */
        k = (unsigned char)((i + phase) % 5);
        c = (unsigned char)(k < 2 ? CBYTE (7, 7) : CBYTE (1, 9));
        c4[2 * i + 1] = c;
    }
}

/* The pleats.  A curtain twenty columns deep painted in one flat red is a
** red rectangle; what makes it fabric is that the luminance repeats on a
** short cycle, so the eye reads folds.  Four columns to a pleat, and the
** three columns nearest the outer edge are pulled down into the shadow of
** the wings on top of that. */
static const unsigned char fold[4] = { 2, 4, 6, 3 };

/* One column of curtain, both sides.  `d` is the distance in from the
** outer edge; the leading edge carries a gold trim, which is what stops
** the whole thing from looking like a wall. */
static void curtain_column (unsigned char d, unsigned char leading)
{
    unsigned char y;
    unsigned char lum = fold[d & 3];
    unsigned char cl;
    unsigned char l   = d;
    unsigned char r   = (unsigned char)(39 - d);

    if (d < 3) {
        unsigned char shade = (unsigned char)(3 - d);
        lum = (unsigned char)(lum > shade ? lum - shade : 1);
    }
    cl = leading ? CBYTE (6, 7) : CBYTE (lum, 2);

    for (y = STAGE_TOP; y <= STAGE_BOT; ++y) {
        rowtab[y][l] = CH_SOLID;
        rowtab[y][r] = CH_SOLID;
        *(rowtab[y] + l - SCREEN_COLOR_D) = cl;
        *(rowtab[y] + r - SCREEN_COLOR_D) = cl;
    }
}

/* The whole curtain, from the outer edge in to `edge`, trim on the leading
** column.  Repainting all of it each step is what lets the stage's own
** contents be redrawn underneath between steps: the dressing goes down
** first and the fabric goes over the top of it. */
static void curtain_all (unsigned char edge)
{
    unsigned char d;

    for (d = 0; d < edge; ++d)
        curtain_column (d, (unsigned char)(d == edge - 1));
}

/* Clear one column pair back to bare stage. */
static void stage_column (unsigned char d)
{
    unsigned char y;
    unsigned char l = d;
    unsigned char r = (unsigned char)(39 - d);

    for (y = STAGE_TOP; y <= STAGE_BOT; ++y) {
        rowtab[y][l] = CH_SPACE;
        rowtab[y][r] = CH_SPACE;
        *(rowtab[y] + l - SCREEN_COLOR_D) = 0;
        *(rowtab[y] + r - SCREEN_COLOR_D) = 0;
    }
}

/* The floor and the credit engraved into it.  Reverse video draws its
** glyph in the BACKGROUND colour, so engraving is not an effect here - it
** is what reverse video does on a black background. */
static void stage_floor (unsigned char edge)
{
    unsigned char w;

    if (edge > 17) return;              /* the curtains have covered it   */
    w = (unsigned char)(40 - 2 * edge);
    blit_ptr = rowtab[STAGE_BOT] + edge;
    blit_ch  = CH_SOLID;
    blit_cl  = CBYTE (4, 8);
    blit_run (w);

    /* Twenty-four characters in a twenty-eight column aperture: the credit
    ** has to fit between the wings, not across the screen.
    **
    ** The build date is NOT here, though this is where it first went: the
    ** menu scrolls in over rows 12 to 22 afterwards and wiped it.  It is
    ** drawn by menu_items instead, with the other lines that have to
    ** survive the scroll. */
    if (edge <= CURTAIN_OPEN)
        text_put_rev (8, STAGE_BOT, "paul richeson  mit  2026", CBYTE (4, 8));
}

void theatre_draw (unsigned char edge)
{
    unsigned char i;

    TED_BGCOLOR = CBYTE (0, 0);
    TED_BORDER  = CBYTE (2, 9);
    screen_fill (CH_SPACE, 0);

    lamp_band (0, 0);
    frieze_row (1, 0);
    frieze_row (3, 2);
    lamp_band (4, 1);
    lamp_band (24, 0);                  /* the footlight rail */

    /* The banner row: zigzag flanks in the wings, the name in the middle. */
    for (i = 0; i < CURTAIN_OPEN; ++i) {
        unsigned char ch = (unsigned char)((i & 1) ? CH_DIAG_L : CH_DIAG_R);
        rowtab[2][i] = ch;
        rowtab[2][39 - i] = ch;
        *(rowtab[2] + i - SCREEN_COLOR_D) = CBYTE (3, 9);
        *(rowtab[2] + 39 - i - SCREEN_COLOR_D) = CBYTE (3, 9);
    }
    text_put (14, 2, "* ur finkel *", CBYTE (7, 7));

    /* The pelmet, and the valance's fringe below it. */
    blit_ptr = rowtab[5];
    blit_ch  = CH_SOLID;
    blit_cl  = CBYTE (2, 2);
    blit_run (SCR_W);
    for (i = 0; i < SCR_W; ++i) {
        if ((i & 3) == 3) continue;
        rowtab[6][i] = CH_SOLID;
        *(rowtab[6] + i - SCREEN_COLOR_D) = CBYTE (3, 2);
    }

    curtain_all (edge);
    stage_floor (edge);
}

/* --- the curtains ----------------------------------------------------- */

/* Fourteen columns of travel, at twelve frames a column: 168 frames, and
** the fanfare is 172.  They are meant to land together - the curtain is
** the fanfare made visible, which is the whole reason the fanfare was
** written with six deliberate steps in the first place. */
#define OPEN_PACE       12
#define CLOSE_PACE      9

/* Shut the curtains in no time at all, so the screen behind them can be
** dressed before they open.  There is no wait in here, so nothing that
** happens between drawing the stage and covering it is ever seen. */
void curtain_shut (void)
{
    curtain_all (CURTAIN_SHUT);
}

/* Opening reveals whatever `dress` paints.  There is no off-screen copy of
** the stage on a machine with 64 KB and a video matrix at a fixed address,
** so a column cannot simply be uncovered: it is cleared, the stage's own
** contents are laid down again, and the remaining fabric goes back over
** the top.  Painting the whole curtain each step costs 680 bytes and buys
** a reveal that works for any content the caller cares to draw. */
void curtain_open (void (*dress) (unsigned char edge))
{
    unsigned char d;

    DBG_ENTER (DBG_FRT, "open");
    music_song (SONG_FANFARE, 0);
    /* d is the curtain's edge: it covers columns 0..d-1 and the mirror of
    ** them.  Fourteen steps from meeting in the middle out to the wings. */
    for (d = CURTAIN_SHUT; d > CURTAIN_OPEN; --d) {
        stage_column ((unsigned char)(d - 1));
        if (dress) dress ((unsigned char)(d - 1));
        curtain_all ((unsigned char)(d - 1));
        stage_floor ((unsigned char)(d - 1));           /* floor widens    */
        wait_frames_live (OPEN_PACE);
    }
    /* The fanfare's held tonic is still sounding; it runs out into the bed
    ** by itself, so nothing here has to cut it off. */
    DBG_LEAVE ();
}

void curtain_close (void)
{
    unsigned char d;

    DBG_ENTER (DBG_FRT, "shut");
    for (d = CURTAIN_OPEN; d < CURTAIN_SHUT; ++d) {
        curtain_column ((unsigned char)(d - 1), 0);     /* trim moves on   */
        curtain_column (d, 1);
        sfx (SFX_STEP);
        wait_frames_live (CLOSE_PACE);
    }
    wait_frames_live (25);                   /* a beat, with the stage covered */
    DBG_LEAVE ();
}

/* --- the trophy ------------------------------------------------------- */

/* BASIC 6280-6285: the cup's left edge, right edge and a luminance debit
** per row.  Nine rows - bowl, stem, foot - and the shading comes from how
** far a cell is from the middle of the screen, so the gold rounds. */
static const unsigned char cup_l[9]  = { 12, 13, 14, 16, 18, 19, 18, 16, 13 };
static const unsigned char cup_r[9]  = { 27, 26, 25, 23, 21, 20, 21, 23, 26 };
static const unsigned char cup_dm[9] = {  2,  0,  0,  1,  1,  2,  2,  3,  3 };

#define TROPHY_TOP      15

/* The name, copied out of the caller's string before anything else runs.
** See the note in trophy_draw. */
static char engraved[13];

void trophy_draw (const char* winner)
{
    unsigned char i, x, y, n, x0;
    signed int    off;
    unsigned char lum;

    /* Take a copy of the name FIRST, before a single frame is waited.
    **
    ** cc65 passes a pointer argument on its software stack and reloads it
    ** through `(sp),y` at each use.  This function's later uses of
    ** `winner` are separated from its entry by sixty-six frames of
    ** wait_frames, and every one of those frames services the music and
    ** runs the raster interrupt two hundred times.  Read after all that,
    ** the pointer no longer refers to the name: the string length comes
    ** back zero and the engraving loop silently does nothing, which is
    ** exactly what the compiled build did while a build with three extra
    ** debug statements in it - different codegen, different stack layout -
    ** engraved correctly.
    **
    ** So the argument is consumed once, immediately, into storage this
    ** function owns.  The rule this is an instance of: on this toolchain,
    ** do not hold a pointer argument across an animation. */
    DBG_ENTER (DBG_FRT, "cup");
    for (n = 0; n < sizeof (engraved) - 1 && winner[n]; ++n)
        engraved[n] = winner[n];
    engraved[n] = 0;

    /* Row by row, at six frames a row: the cup is poured rather than
    ** stamped, which is a second of screen time well spent at the one
    ** moment in the match nobody is in a hurry. */
    for (i = 0; i < 9; ++i) {
        y = (unsigned char)(TROPHY_TOP + i);
        for (x = cup_l[i]; x <= cup_r[i]; ++x) {
            off = (signed int)(2 * (signed int)x - 39);
            if (off < 0) off = -off;
            lum = (unsigned char)(7 - (off / 6) - cup_dm[i]);
            if ((signed char)lum < 2) lum = 2;
            rowtab[y][x] = CH_SOLID;
            *(rowtab[y] + x - SCREEN_COLOR_D) = CBYTE (lum, 7);
        }
        wait_frames_live (6);
    }

    /* The handles, in a flatter gold so they read as behind the bowl. */
    for (y = 16; y <= 18; ++y) {
        rowtab[y][9]  = CH_SOLID;
        rowtab[y][30] = CH_SOLID;
        *(rowtab[y] + 9  - SCREEN_COLOR_D) = CBYTE (4, 7);
        *(rowtab[y] + 30 - SCREEN_COLOR_D) = CBYTE (4, 7);
    }
    rowtab[15][10] = CH_SOLID;  rowtab[15][29] = CH_SOLID;
    rowtab[19][10] = CH_SOLID;  rowtab[19][29] = CH_SOLID;
    *(rowtab[15] + 10 - SCREEN_COLOR_D) = CBYTE (4, 7);
    *(rowtab[15] + 29 - SCREEN_COLOR_D) = CBYTE (4, 7);
    *(rowtab[19] + 10 - SCREEN_COLOR_D) = CBYTE (4, 7);
    *(rowtab[19] + 29 - SCREEN_COLOR_D) = CBYTE (4, 7);
    wait_frames_live (12);

    /* And the name cut into the bowl, a letter at a time.  Reverse video
    ** over the gold puts the letter in the background colour, so it is a
    ** true engraving rather than dark text laid on top. */
    x0 = (unsigned char)(20 - (n >> 1));
    for (i = 0; i < n; ++i) {
        rowtab[17][x0 + i] =
            (unsigned char)(scr_code ((unsigned char)engraved[i]) | CH_REVERSE);
        sfx (SFX_CLICK);
        wait_frames_live (4);
    }
}
