/* ------------------------------------------------------------------------
 * anim.c - the animation gallery.  One effect, in isolation, on demand.
 *
 * Every moving thing in this game is wired into a moment: the curtain only
 * opens on the way to the menu, the trophy is only poured when somebody
 * wins, and the laser only cuts when a piece is taken.  That makes them
 * expensive to look at and almost impossible to compare - to see the
 * tumble you have to play a turn, and to see the trophy you have to play a
 * match.
 *
 * This runs one of them, alone, against a plain background, and parks.
 * Which one is chosen at compile time with -DANIM=n, so each is a separate
 * binary and a screenshot of it is deterministic: no game state, no
 * randomness that matters, nothing else on screen to confuse what changed.
 *
 *     make anim              the gallery, as one contact sheet
 *     make anim-run ANIM=3   one of them, in the emulator, to watch
 *
 * The numbering is in tools/anim.py, which is also what builds the sheet.
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "rules.h"
#include "board.h"
#include "text.h"
#include "dice.h"
#include "front.h"
#include "music.h"
#include "etch.h"

#ifndef ANIM
#  define ANIM 0
#endif

/* The board, dressed enough that the effects which sit on it have
** something to sit on.  Several of them - the shimmer, the flash, the
** glide - are meaningless against an empty screen, because what they do is
** modify a board that is already there. */
static void with_board (void)
{
    board_init ();
    shade_profile (SHADE_SOFT);
    reset_board ();
    board_draw ();
}

static void label (const char* s)
{
    text_put (1, 24, s, CBYTE (5, 1));
}

void main (void)
{
    unsigned char i;

    board_init ();
    shade_profile (SHADE_SOFT);
    music_init ();
    TED_BGCOLOR = CBYTE (0, 0);
    TED_BORDER  = CBYTE (0, 0);
    screen_fill (CH_SPACE, 0);

#if ANIM == 0
    /* THE LASER, cutting a word.  What a capture shows. */
    label ("0  laser etch - text");
    for (;;) {
        etch_text ("captured", 12, 100);
        wait_frames_live (60);
        rows_fill (10, 14, CH_SPACE, 0);
    }

#elif ANIM == 1
    /* THE LASER, cutting the two letters from a bitmap.  The way in. */
    label ("1  laser etch - big ur");
    for (;;) {
        etch_big_ur (250);
        wait_frames_live (80);
        rows_fill (8, 18, CH_SPACE, 0);
    }

#elif ANIM == 2
    /* THE SHIMMER.  A sheen drifting along each tile's face - the outer
    ** bands leftward, the middle band rightward - every tile out of phase
    ** with its neighbours.  Needs the board: it modifies the highlight and
    ** shadow rows of tiles that are already drawn. */
    with_board ();
    label ("2  shimmer - drift");
    for (;;) wait_frames_live (1);

#elif ANIM == 3
    /* THE TURN SWEEP.  The flare that marks the players changing hands. */
    with_board ();
    label ("3  turn sweep");
    for (;;) {
        for (i = 0; i < 6; ++i) {
            TED_BGCOLOR = CBYTE ((unsigned char)(7 - i), 9);
            TED_BORDER  = CBYTE ((unsigned char)(i + 2), 9);
            wait_frames_live (6);
        }
        TED_BGCOLOR = CBYTE (0, 0);
        TED_BORDER  = CBYTE (0, 0);
        wait_frames_live (40);
    }

#elif ANIM == 4
    /* THE TUMBLE.  Four tetrahedral lots scattered across the floor and
    ** settling one at a time. */
    with_board ();
    label ("4  the lots tumble");
    thrower_colour = CBYTE (6, 9);
    for (;;) {
        (void)throw_lots ();
        wait_frames_live (60);
        floor_wipe ();
    }

#elif ANIM == 5
    /* THE CURTAIN, and the lamp chase that runs while it moves. */
    label ("5  curtain and lamps");
    for (;;) {
        theatre_draw (CURTAIN_SHUT);
        curtain_open (0);
        for (i = 0; i < 60; ++i) { lamps_frame (i); wait_frames_live (2); }
        curtain_close ();
    }

#elif ANIM == 6
    /* THE TROPHY, poured a row at a time with a name cut into the bowl. */
    label ("6  the trophy");
    for (;;) {
        screen_fill (CH_SPACE, 0);
        label ("6  the trophy");
        trophy_draw ("urbot");
        wait_frames_live (80);
    }

#elif ANIM == 7
    /* THE GLIDE.  A piece travelling one character cell at a time along
    ** the line between two squares, rather than jumping a square. */
    with_board ();
    label ("7  piece glide");
    for (;;) {
        glide (piece_cell_x (0, 1), piece_cell_y (0, 1),
               piece_cell_x (0, 4), piece_cell_y (0, 4),
               (unsigned char)(CH_PIECE_BASE + 1), CBYTE (6, 9), 2);
        wait_frames_live (25);
        board_draw ();
        label ("7  piece glide");
    }

#elif ANIM == 8
    /* THE MENU SCROLL.  The choices climbing into the open stage. */
    label ("8  menu scroll-in");
    for (;;) {
        theatre_draw (CURTAIN_OPEN);
        for (i = 11; i > 0; --i) {
            rows_fill (12, 22, CH_SPACE, 0);
            text_centre ((unsigned char)(12 + i - 1), "player vs urbot",
                         CBYTE (7, 1));
            text_centre ((unsigned char)(13 + i - 1), "two players",
                         CBYTE (7, 1));
            wait_frames_live (5);
        }
        wait_frames_live (60);
    }

#elif ANIM == 10
    /* THE BURN, on the whole stage.  What a capture shows: a bed of fire
    ** across the bottom six rows and the word in block letters above it,
    ** catching from its feet upward, glyphs morphing through flame rather
    ** than colours changing under a fixed character.  Against the real
    ** board, because the apron it takes over is the board's own floor. */
    /* THE CHRONICLE IS THE TEST, and it is written here rather than a
    ** label being put at the foot of the screen.  The fire covers the log
    ** as well as the floor, so the thing most likely to be wrong is not
    ** the fire - it is whether four lines of words come back afterwards.
    ** They are in the chronicle's own buffer and apron_clear repaints
    ** them; a shot taken in the pause after the fire says whether that is
    ** true. */
    with_board ();
    say ("10  burn - the apron alight");
    say ("the fire takes these four lines too");
    say ("urbot a captures a foe on 8");
    say ("...and the log must come back");
    for (;;) {
        burn_apron ("captured", 190);
        wait_frames_live (40);
        apron_clear ();
        wait_frames_live (120);         /* long enough to be caught       */
    }

#elif ANIM == 12
    /* THE WHOLE END OF A MATCH, in the order victory() does it and with
    ** the same numbers.
    **
    ** Every other entry here shows ONE effect; this one shows a SEQUENCE,
    ** because the things that go wrong at the end of a match are not
    ** inside any single effect - they are between them. Does the cup land
    ** on top of the name. Does the wipe between them take the two lines
    ** above it as well. Does the border come back to the winner's colour
    ** when the last shell dies, or stay stuck on a firework's hue.
    **
    ** None of that is reachable from the game without playing a match to
    ** a win, which is why it is here: a screenshot of the real victory is
    ** a screenshot of a demo that happened to be finishing. */
    TED_BORDER = CBYTE (6, 9);          /* stand in for the winner's colour */
    text_centre (13, "urfinkel wins the royal game of ur", CBYTE (7, 7));
    text_centre (14, "the other side got 3 home", CBYTE (5, 7));
    for (;;) {
        etch_word ("urfinkel", 16, 200);
        wait_frames_live (60);
        rows_fill (APRON_TOP + 1, APRON_BOT, CH_SPACE, 0);
        trophy_draw ("urfinkel");
        fireworks (250);
        wait_frames_live (60);
        rows_fill (APRON_TOP + 1, APRON_BOT, CH_SPACE, 0);
    }

#elif ANIM == 9
    /* THE FIREWORKS.  Three shells in the air at once over a name, as the
    ** winner gets, and the border lighting up on every burst. */
    label ("9  fireworks");
    for (;;) {
        etch_word ("urbot", 16, 90);
        fireworks (250);
        wait_frames_live (30);
        rows_fill (4, 22, CH_SPACE, 0);
        label ("9  fireworks");
    }

#elif ANIM == 11
    /* THE WINNER'S NAME, cut across the whole stage in block letters -
    ** eight characters at a five column pitch is thirty-nine of the
    ** forty. */
    with_board ();
    label ("11  block letters - the win");
    for (;;) {
        etch_word ("urfinkel", 16, 200);
        wait_frames_live (60);
        apron_clear ();
        label ("11  block letters - the win");
        wait_frames_live (30);
    }

#else
    label ("no such animation");
    for (;;) wait_frames_live (1);
#endif
}
