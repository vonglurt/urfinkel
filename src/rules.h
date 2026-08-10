/* ------------------------------------------------------------------------
 * rules.h - the Finkel ruleset, with nothing about the Plus/4 in it.
 *
 * This module is deliberately free of screen addresses, TED registers and
 * cc65 extensions, so it compiles for the host as well as for the target.
 * That is what lets the thirteen rule checks from the frozen edition's
 * mode 6 run on macOS in milliseconds instead of a minute of emulation -
 * see `make check`.
 *
 * The path model is carried over unchanged: each player has a private
 * 1-D path of 14 squares, and squares 5-12 map to the same physical cells
 * for both players, which is where capture applies.
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_RULES_H
#define URFINKEL_RULES_H

#define PIECES          7
#define PATH_LAST       14
#define SQ_POOL         0
#define SQ_HOME         15
#define SHARED_FIRST    5               /* the corridor both players walk */
#define SHARED_LAST     12
#define SQ_SAFE_ROSETTE 8               /* the one rosette that is shared */

/* piece[player][1..7]: 0 = pool, 1-14 = on the board, 15 = home.
** Index 0 of each row is unused, so piece numbers read as they do on
** screen and in the chronicle. */
extern unsigned char piece[2][PIECES + 1];

/* 1 if that path square is a rosette - 4, 8 and 14. */
extern const unsigned char rosette[SQ_HOME + 1];

/* Filled by find_legal_moves: legal[j] is the destination of piece j this
** throw, or 0 if it may not move.  BASIC's V(j) and NV. */
extern unsigned char legal[PIECES + 1];
extern unsigned char legal_count;

/* Filled by execute_move.  Globals rather than a returned struct because
** cc65 passes structs expensively and every caller wants all five. */
extern unsigned char mv_from;           /* the square it left            */
extern unsigned char mv_to;             /* the square it reached         */
extern unsigned char mv_captures;       /* enemy pieces sent to the pool */
extern unsigned char mv_rosette;        /* 1 = landed on a rosette       */
extern unsigned char mv_won;            /* 1 = that was the seventh home */

/* The move generator - BASIC 3500.  Enforces the three things that make a
** move illegal: a friendly piece already on the destination, the central
** rosette while a foe holds it, and an overshoot past square 15. */
void find_legal_moves (unsigned char player, unsigned char roll);

/* The move executor - BASIC 5000.  Applies capture, the move itself, the
** rosette extra throw and the win check, and reports all four.  It does
** not draw anything: animation is the caller's business, which is what
** keeps this file host-testable. */
void execute_move (unsigned char player, unsigned char pc);

/* Census helpers - BASIC counts these inline in four different places. */
unsigned char count_at_home (unsigned char player);
unsigned char count_in_pool (unsigned char player);
unsigned char count_afield (unsigned char player);

void reset_board (void);

#endif
