/* ------------------------------------------------------------------------
 * board.h - the board, as pixels rather than as rules.
 *
 * The compiled port of the BASIC edition's renderer: the table (line
 * 1200), the twenty buttons (1220), the tile painter (7200), the cell
 * painter (7000) and the waist plaques (7700-7799).  Geometry, colour
 * bytes and screen codes are carried over unchanged, on purpose - the two
 * editions must paint the same screen, so that the compiled port can be
 * judged against a screenshot of the frozen one (`make conform`).
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_BOARD_H
#define URFINKEL_BOARD_H

#include "rules.h"

/* __fastcall__ is a cc65 extension.  The rules modules are also compiled
** for the host so their unit tests can run on macOS in milliseconds, and
** a host compiler has never heard of it. */
#ifndef __CC65__
#  define __fastcall__
#endif

/* --- the assembly primitive (blit.s) ---------------------------------- */

extern unsigned char* blit_ptr;         /* destination in the screen matrix */
extern unsigned char  blit_ch;          /* screen code                      */
extern unsigned char  blit_cl;          /* colour byte                      */
void __fastcall__ blit_run (unsigned char n);

/* --- presentation state the renderer reads ---------------------------- */
/* The board state itself (piece[][]) belongs to rules.h; these are the
** BASIC edition's PC(pl), PL(pl) and N$(pl). */

extern unsigned char player_hue[2];     /* 1-16, as the picker reports it  */
extern unsigned char player_lum[2];     /* 0-7                             */
extern const char*   player_name[2];

/* --- geometry (BASIC 130-200, precomputed at boot) -------------------- */

extern unsigned char sq_x[2][SQ_HOME];  /* screen column of a path square  */
extern unsigned char sq_y[2][SQ_HOME];  /* screen row                      */
extern unsigned char sq_c[2][SQ_HOME];  /* its base colour byte            */

extern unsigned char* rowtab[25];       /* address of each screen row      */
extern const unsigned char band_y[3];   /* first row of each band of tiles */
#define BOARD_ROWS      14              /* the board occupies rows 0..13   */

/* --- rendering -------------------------------------------------------- */

void board_init (void);                 /* row table, geometry, charset    */
void screen_fill (unsigned char ch, unsigned char cl);
void rows_fill (unsigned char y0, unsigned char y1,
                unsigned char ch, unsigned char cl);
void paint_button (unsigned char x, unsigned char y,
                   unsigned char base,  /* colour byte: lum*16 + hue       */
                   unsigned char well,  /* 0 empty, 1 rosette, >1 glyph    */
                   unsigned char flood, /* 1 = flood base rows in pcol     */
                   unsigned char pcol); /* the owner's colour byte         */
/* --- how a tile is shaded --------------------------------------------- */
/* The frozen edition spread one 4x4 square over five of the machine's
** eight luminance steps.  SHADE_SOFT halves that; SHADE_LEGACY is the
** original and is what `make conform` requires, since that test asserts a
** screenshot byte-identical to the BASIC edition's.  board_init leaves the
** legacy profile in place, so the oracle needs no special case - it is the
** GAME that opts into the soft one. */
#define SHADE_LEGACY    0
#define SHADE_SOFT      1
void shade_profile (unsigned char p);

/* --- the shimmer ------------------------------------------------------ */
/* A band of light crossing the board left to right, lifting the highlight
** row of the tiles it passes over and touching nothing else.  Colour RAM
** only, so it can never disturb a glyph or a piece.  screen_fill switches
** it off by itself; board_draw switches it on. */
void shimmer_enable (unsigned char on);
void shimmer_frame (void);              /* one step; called once a frame  */

void paint_table (void);                /* the gutters only                */
void paint_buttons (void);              /* the twenty squares              */
void paint_plaques (void);              /* both waist plaques, whole       */
void paint_tokens (void);               /* both token rows only            */
void draw_cell (unsigned char player, unsigned char square);  /* BASIC 7000 */

/* draw_cell with the tile's colour overridden - what the move animation
** fades a square with.  pcol 0 leaves the occupant its own colour. */
void square_tint (unsigned char player, unsigned char square,
                  unsigned char base, unsigned char pcol);

void board_draw (void);                 /* everything: the BASIC 1000      */

/* --- whose turn it is, as light --------------------------------------- */
/* Raise player_dim[q] and that player's four rows of squares and their
** waist plaque recede three luminance steps, and the shimmer stops
** touching them.  The bridge belongs to both and never dims.  Call
** board_relight after changing it. */
extern unsigned char player_dim[2];
void board_relight (void);

/* --- animation -------------------------------------------------------- */

/* Glide a glyph from one character cell to another, restoring whatever it
** covered on the way.  This is the motion tweening the BASIC edition
** could not afford: it moved a piece one whole board square per step,
** because a square is 32 POKEs and a POKE was 23 ms. */
void glide (unsigned char x0, unsigned char y0,
            unsigned char x1, unsigned char y1,
            unsigned char ch, unsigned char cl, unsigned char pace);

/* The cell a piece's glyph occupies inside its square's well. */
unsigned char piece_cell_x (unsigned char player, unsigned char square);
unsigned char piece_cell_y (unsigned char player, unsigned char square);

/* Two waits, and the difference between them is load-bearing.
**
**   wait_frames       bare.  Used by the innermost drawing loops.
**   wait_frames_live  the same, plus one music_service a frame.
**
** Service the sequencer from shallow call sites only - a controller wait,
** a throw, a curtain step.  Doing it from inside glide's per-cell loop as
** well wedged the machine; see the note over wait_frames_live in board.c
** before moving that line. */
void wait_frames (unsigned char n);
void wait_frames_live (unsigned char n);

#endif
