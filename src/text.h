/* ------------------------------------------------------------------------
 * text.h - words on the screen, and the one place they go.
 *
 * Two things live here.  The first is a plain text writer: PETSCII or
 * ASCII in, screen codes out, straight into the two matrices.  The second
 * is the chronicle - the BASIC edition's `8000`, the single routine every
 * word the program says passes through on its way to the four scrolling
 * lines at the foot of the screen.
 *
 * Sentences are assembled with the line builder rather than with printf,
 * which on cc65 would drag in a formatter far larger than the whole
 * renderer for the sake of "%d".
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_TEXT_H
#define URFINKEL_TEXT_H

#define CHRON_TOP       21              /* the four chronicle rows        */
#define CHRON_LINES     4
#define CHRON_WIDTH     37
#define CHRON_LEFT      2

/* --- the writer ------------------------------------------------------- */

void text_put (unsigned char x, unsigned char y, const char* s,
               unsigned char col);
void text_put_rev (unsigned char x, unsigned char y, const char* s,
                   unsigned char col);
void text_centre (unsigned char y, const char* s, unsigned char col);

/* Centred, but only the characters that fall inside [left, right].
**
** For revealing a line through a widening aperture: the middle of the
** string is written first and letters are added at both ends as the window
** opens, so the text arrives WITH the opening rather than sitting behind
** it waiting to be uncovered. */
void text_centre_win (unsigned char y, const char* s, unsigned char col,
                      unsigned char left, unsigned char right);

/* --- the line builder ------------------------------------------------- */
/* Assemble a sentence, then hand it to say().  ln_reset starts a fresh
** line; the rest append and never overrun. */

extern char ln_buf[41];

void ln_reset (void);
void ln_str (const char* s);
void ln_num (unsigned char n);
void ln_ch (char c);

/* --- the chronicle ---------------------------------------------------- */

/* Every prompt, roll, capture and thought goes through here, so there is
** exactly one place that knows about the panel's geometry, padding and
** colour.  Because the panel scrolls, a multi-step deliberation stays on
** screen as a readable trace instead of overwriting itself - which is the
** whole point for URBOT. */
void say (const char* s);
void say_line (void);                   /* say (ln_buf) */
void chronicle_reset (void);

/* Put the four lines back after something has been drawn over them - the
** lots roll across the top one.  See the note in text.c. */
void chronicle_redraw (void);

/* Render suppression, the BASIC edition's ND flag: with this raised the
** rules can be driven with the screen held still. */
extern unsigned char no_draw;

#endif
