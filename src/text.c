/* ------------------------------------------------------------------------
 * text.c - the writer and the chronicle.
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "board.h"
#include "text.h"

unsigned char no_draw;

extern unsigned char scr_code (unsigned char c);

/* --- the writer ------------------------------------------------------- */

static void put (unsigned char x, unsigned char y, const char* s,
                 unsigned char col, unsigned char rev)
{
    unsigned char* r = rowtab[y] + x;
    unsigned char* c = r - SCREEN_COLOR_D;
    unsigned char  i = 0;

    while (s[i] && (unsigned char)(x + i) < SCR_W) {
        r[i] = (unsigned char)(scr_code ((unsigned char)s[i]) | rev);
        c[i] = col;
        ++i;
    }
}

void text_put (unsigned char x, unsigned char y, const char* s,
               unsigned char col)
{
    put (x, y, s, col, 0);
}

void text_put_rev (unsigned char x, unsigned char y, const char* s,
                   unsigned char col)
{
    put (x, y, s, col, CH_REVERSE);
}

static unsigned char slen (const char* s)
{
    unsigned char n = 0;
    while (s[n]) ++n;
    return n;
}

void text_centre (unsigned char y, const char* s, unsigned char col)
{
    unsigned char n = slen (s);
    put ((unsigned char)(n >= SCR_W ? 0 : (SCR_W - n) >> 1), y, s, col, 0);
}

void text_centre_win (unsigned char y, const char* s, unsigned char col,
                      unsigned char left, unsigned char right)
{
    unsigned char  n  = slen (s);
    unsigned char  x0 = (unsigned char)(n >= SCR_W ? 0 : (SCR_W - n) >> 1);
    unsigned char* r  = rowtab[y];
    unsigned char* c  = r - SCREEN_COLOR_D;
    unsigned char  i, x;

    if (right >= SCR_W) right = SCR_W - 1;
    for (i = 0; i < n; ++i) {
        x = (unsigned char)(x0 + i);
        /* Outside the window the character is simply not written - not
        ** written as a space, which would punch a hole in the curtain
        ** that is meant to be covering it. */
        if (x < left || x > right) continue;
        r[x] = scr_code ((unsigned char)s[i]);
        c[x] = col;
    }
}

/* --- the line builder ------------------------------------------------- */

char ln_buf[41];
static unsigned char ln_len;

void ln_reset (void)
{
    ln_len = 0;
    ln_buf[0] = 0;
}

void ln_ch (char c)
{
    if (ln_len >= sizeof (ln_buf) - 1) return;
    ln_buf[ln_len++] = c;
    ln_buf[ln_len]   = 0;
}

void ln_str (const char* s)
{
    while (*s) ln_ch (*s++);
}

void ln_num (unsigned char n)
{
    /* Up to three digits, no leading zeros, no division library call in
    ** the common cases - cc65's byte divide is cheap but this is cheaper
    ** and the range is known. */
    if (n >= 100) { ln_ch ((char)('0' + n / 100)); n %= 100; ln_ch ((char)('0' + n / 10)); }
    else if (n >= 10) { ln_ch ((char)('0' + n / 10)); }
    ln_ch ((char)('0' + n % 10));
}

/* --- the chronicle ---------------------------------------------------- */

static char lines[CHRON_LINES][CHRON_WIDTH + 1];

/* Oldest dimmest.  The luminance ramp is what marks the panel as a
** scrolling log without needing a caption - which is why it has no
** header, unlike the BASIC edition before its legibility pass. */
static const unsigned char lum[CHRON_LINES] = { 3, 4, 5, 7 };

/* Repaint the four lines from the buffer, disturbing nothing else.
**
** The lots roll across CHRON_TOP now - the board grew two rows and the
** casting floor took them out of the top of the log - so the throw leaves
** glyphs and erased cells on a line that still has words on it. The words
** are kept in `lines`, so putting them back is a repaint rather than a
** redraw: this is the same "restore what you disturbed" discipline the
** piece glide uses, one row higher up. */
void chronicle_redraw (void)
{
    unsigned char i;

    if (no_draw) return;
    for (i = 0; i < CHRON_LINES; ++i) {
        blit_ptr = rowtab[CHRON_TOP + i];
        blit_ch  = CH_SPACE;
        blit_cl  = 0;
        blit_run (SCR_W - 1);
        *rowtab[CHRON_TOP + i] = CH_RULE;
        *(rowtab[CHRON_TOP + i] - SCREEN_COLOR_D) = CBYTE (5, 7);
        text_put (CHRON_LEFT, (unsigned char)(CHRON_TOP + i), lines[i],
                  CBYTE (lum[i], 1));
    }
}

void chronicle_reset (void)
{
    unsigned char i, j;

    for (i = 0; i < CHRON_LINES; ++i)
        for (j = 0; j <= CHRON_WIDTH; ++j)
            lines[i][j] = 0;

    if (no_draw) return;
    for (i = 0; i < CHRON_LINES; ++i) {
        blit_ptr = rowtab[CHRON_TOP + i];
        blit_ch  = CH_SPACE;
        blit_cl  = 0;
        blit_run (SCR_W - 1);
        *rowtab[CHRON_TOP + i] = CH_RULE;
        *(rowtab[CHRON_TOP + i] - SCREEN_COLOR_D) = CBYTE (5, 7);
    }
}

void say (const char* s)
{
    unsigned char i, j;

    if (no_draw) return;

    for (i = 0; i < CHRON_LINES - 1; ++i)               /* scroll up */
        for (j = 0; j <= CHRON_WIDTH; ++j)
            lines[i][j] = lines[i + 1][j];

    /* Pad to the panel width and clip to it, so a short line wipes what
    ** the previous occupant of that row left behind. */
    j = 0;
    while (j < CHRON_WIDTH && s[j]) { lines[CHRON_LINES - 1][j] = s[j]; ++j; }
    while (j < CHRON_WIDTH)         { lines[CHRON_LINES - 1][j] = ' '; ++j; }
    lines[CHRON_LINES - 1][CHRON_WIDTH] = 0;

    for (i = 0; i < CHRON_LINES; ++i)
        text_put (CHRON_LEFT, (unsigned char)(CHRON_TOP + i),
                  lines[i], CBYTE (lum[i], 7));
}

void say_line (void)
{
    say (ln_buf);
}
