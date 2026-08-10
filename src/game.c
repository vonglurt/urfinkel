/* ------------------------------------------------------------------------
 * game.c - the controller, the front end, and the end of a match.
 *
 * The shape is the frozen edition's, because it was right:
 *
 *   set turn colours -> wipe chronicle and floor -> throw -> legal moves
 *   -> choose piece (human or AI) -> execute and animate -> win/rosette
 *   -> switch player -> repeat
 *
 * What changed is that nothing blocks any more.  Waiting on a human goes
 * through poll_key and waiting on the clock goes through wait_frames, and
 * both service the music sequencer on the way past - so the ambient bed
 * keeps playing and the dice keep being unpredictable whether the game is
 * sitting on a prompt or running an animation.  The BASIC edition had to
 * choose between waiting and doing; this one does not.
 * --------------------------------------------------------------------- */

#include <conio.h>
#include "plus4.h"
#include "rules.h"
#include "board.h"
#include "text.h"
#include "dice.h"
#include "urbot.h"
#include "music.h"
#include "front.h"
#include "kbd.h"
#include "etch.h"
#include "dbg.h"

#define MODE_VS_BOT     1
#define MODE_TWO        2
#define MODE_MANUAL     3
#define MODE_DEMO       4
#define MODE_RULES      5

/* Two minutes at 50 Hz.  Overridable at compile time so the smoke test can
** reach gameplay without waiting out the real thing:
**     make test SMOKEFLAGS=-DATTRACT_FRAMES=250 */
#ifndef ATTRACT_FRAMES
#  define ATTRACT_FRAMES 6000
#endif

/* --- how long the game takes to say things ---------------------------
**
** These were loose numbers scattered through the turn loop, and together
** they are most of a match's wall clock. Named here so the pace is one
** decision rather than nine, and cut roughly in half from what they were:
** the chronicle line is already on screen and readable by the time the
** wait starts, so the wait is only there to stop the next thing treading
** on it. */
/* THE MARQUEE.  How long each cut may take, in frames at 50 Hz - the
** caller owns the clock and the effect fits the word into it, so a long
** word is cut faster rather than for longer.
**
** THESE WERE ALL TOO SMALL AND TOO SHORT, and by the same mistake: the
** two moments that stop the game were being shown in one row of the
** eleven under the board, for two seconds, while the dice - which happen
** every turn - got the other ten rows and one and a half.  A capture and
** a win now take the whole apron (etch.h) and the clock to go with it.
**
** The one that did NOT grow is `home`. It happens up to fourteen times a
** match and the reason to keep it small is the same reason to make the
** other two large: a game where everything is an event has no events. */
#define ETCH_ROW        17      /* mid casting floor, clear between throws */
#define BIG_WIN_ROW     16      /* the winner's name, under its two lines  */
#define ETCH_HOME       80      /* a bear-off: one row, and briefly        */
#define BURN_CAPTURE    190     /* the apron alight, nearly four seconds   */
#define ETCH_WIN        200     /* the name cut across the apron, four     */
#define WIN_HOLD        60      /* and standing, before the cup covers it  */
#define FIREWORK_LEN    250     /* five seconds, a shell every eleven frames*/
#define ETCH_INTRO      250     /* five, under the trumpets                */

#define ETCH_MODE       80      /* cutting the chosen mode's name          */
#define MODE_NAME_ROW   14
#define MODE_LINE_ROW   16
#define MODE_HOLD       45      /* a beat to read it before the curtain    */

#define DEMO_CUP_HOLD   120     /* the unattended look at the cup - shorter
                                ** than it was, because the five seconds of
                                ** fireworks now come first                */
#define SWEEP_PACE      6       /* a step of the turn flare, x6           */
#define BEAT_READ       30      /* long enough to read a line that matters */
#define BEAT_NOTE       25      /* a passing remark - a rosette, a tie     */

#ifndef BUILD_DATE
#  define BUILD_DATE "unknown"
#endif

extern unsigned char scr_code (unsigned char c);
extern volatile unsigned char music_frames;  /* the irq writes it */

static unsigned char mode;
static unsigned char turn;              /* whose turn it is, 0 or 1       */
static unsigned char last_turn;
static char          names[2][9];

/* --- keys ------------------------------------------------------------- */

/* The keyboard is read from TED directly - see kbd.c - and NOT through
** cc65's conio.
**
** conio's kbhit() reads the KERNAL's buffer count at $EF, which the
** KERNAL's periodic interrupt is supposed to fill.  That interrupt does
** not run under cc65's plus4 runtime: `make kbdiag` measures its jiffy
** clock frozen across all three bytes with our raster interrupt installed
** AND removed, while our own handler counts frames normally.  So $EF never
** moved, kbhit() never returned true, and no key ever reached this
** function - which is precisely what "the keyboard does not work" looked
** like from the outside.  The evidence is in docs/keyboard-report.md.
**
** The fix routes around the KERNAL rather than repairing it: on this
** machine TED scans the keyboard itself, so kbd_get reads the matrix
** through $FD30/$FF08 and owes the KERNAL nothing.
**
** The other two things this function does are unchanged and must stay
** that way - the music sequencer and the random stir are serviced here on
** every poll, and neither depends on a key having arrived. */
static unsigned char poll_key (void)
{
    unsigned char k;

    music_service ();
    rnd_stir ();
    k = kbd_get ();
    /* The trace keys are swallowed here, so no screen in the game has to
    ** know they exist and the production build has no branch for them. */
    if (k && DBG_KEY (k)) return 0;
    return k;
}

/* Waiting on a human, one frame at a time.
**
** The frame wait is not politeness, it is what makes the keyboard work:
** wait_frames_live is where the matrix is scanned (see kbd_scan), so a
** spin that does not go through it never scans and would wait for ever.
** It used to spin on poll_key as fast as the processor could manage, which
** scanned thousands of times a second and debounced nothing. */
static unsigned char wait_key (void)
{
    unsigned char k;

    for (;;) {
        k = poll_key ();
        if (k) return k;
        wait_frames_live (1);
    }
}

/* Compare a key against a letter without caring whether the compiler put
** the literal in ASCII or in PETSCII: both land on the same screen code. */
static unsigned char keyis (unsigned char k, char c)
{
    return (unsigned char)(scr_code (k) == scr_code ((unsigned char)c));
}

/* The `s` key, answered from every prompt that reads one.  Effects and
** music are separate switches because they are separate complaints: the
** clicks and thuds are what a spectator tires of, and the bed is what
** they are watching it for. */
static unsigned char sfx_key (unsigned char k)
{
    if (!keyis (k, 's')) return 0;
    music_sfx ((unsigned char)(!music_sfx_on ()));
    say (music_sfx_on () ? "effects on" : "effects off");
    return 1;
}

/* --- text entry ------------------------------------------------------- */

static void input_line (unsigned char x, unsigned char y, char* buf,
                        unsigned char maxlen)
{
    unsigned char n = 0, k;

    for (;;) {
        rowtab[y][x + n] = CH_SPACE | CH_REVERSE;        /* the cursor */
        *(rowtab[y] + x + n - SCREEN_COLOR_D) = CBYTE (7, 1);

        k = wait_key ();
        if (k == 13) break;                             /* return         */
        if (k == 20 || k == 8) {                        /* delete         */
            if (n) {
                rowtab[y][x + n] = CH_SPACE;
                *(rowtab[y] + x + n - SCREEN_COLOR_D) = 0;
                --n;
                rowtab[y][x + n] = CH_SPACE;
            }
            continue;
        }
        if (n >= maxlen) continue;
        if (k < 32) continue;
        buf[n] = (char)k;
        rowtab[y][x + n] = scr_code (k);
        *(rowtab[y] + x + n - SCREEN_COLOR_D) = CBYTE (7, 1);
        ++n;
    }
    rowtab[y][x + n] = CH_SPACE;
    *(rowtab[y] + x + n - SCREEN_COLOR_D) = 0;
    buf[n] = 0;
}

static unsigned char read_num (unsigned char x, unsigned char y,
                               unsigned char lo, unsigned char hi)
{
    char          buf[4];
    unsigned char v, i;

    for (;;) {
        input_line (x, y, buf, 3);
        v = 0;
        for (i = 0; buf[i]; ++i) {
            if (buf[i] < '0' || buf[i] > '9') { v = 255; break; }
            v = (unsigned char)(v * 10 + (buf[i] - '0'));
        }
        if (buf[0] && v >= lo && v <= hi) return v;
        sfx (SFX_ERROR);
    }
}

/* --- the front end ---------------------------------------------------- */

static const char* const hue_name[16] = {
    "black",  "white",   "red",     "cyan",
    "purple", "green",   "blue",    "yellow",
    "orange", "brown",   "yel-grn", "pink",
    "blu-grn","lt blue", "dk blue", "lt green"
};

/* --- the bill of fare, and how it arrives -----------------------------
**
** Every line has to fit the twenty-eight column aperture, not the forty
** column screen: the six columns each side belong to the curtains.
** text_centre still works because the aperture is centred on the screen.
**
** The curtain opens on the TITLE only.  The choices are not behind the
** fabric waiting to be uncovered - they scroll up into the open stage
** afterwards, which is the order a theatre does it in: the house sees
** where it is first, and is told what is on second. */

#define MENU_ROW0       12      /* where the first choice comes to rest   */
#define MENU_MUTE_ROW   19
#define MENU_SFX_ROW    20      /* the `s` key and what it is set to      */
#define MENU_QUIT_ROW   21      /* how to get back here from a match      */
#define MENU_DATE_ROW   22      /* which build this is                    */
#define MENU_ITEMS      5
#define MENU_PACE       5       /* frames per row of travel               */

static const char* const menu_item[MENU_ITEMS] = {
    "1  player vs urbot",
    "2  two players",
    "3  two players, real lots",
    "4  urbot vs urbot demo",
    "5  how we play"
};

/* Indexed by BED_*, so the order here is the order `m` walks. */
static const char* const bed_name[BED_COUNT] = {
#define X(id, label) label,
    BED_SONG_LIST
#undef X
    "the bed", "off"
};

/* The title is written THROUGH the opening, not revealed behind it.
**
** Drawing the whole line every step and letting the curtain cover the ends
** looks like the words were already painted on the back wall and the
** curtain is merely getting out of the way.  Clipping each step to the
** aperture instead means the middle appears first and letters are added at
** both ends as the wings draw back - the line arrives with the opening.
**
** The two are only distinguishable because the clip does not blank what it
** skips: it leaves those cells to the curtain, which is drawn over the top
** immediately afterwards. */
static void menu_content (unsigned char edge)
{
    unsigned char right = (unsigned char)(SCR_W - 1 - edge);

    text_centre_win (8, "the royal game of ur",      CBYTE (7, 7), edge, right);
    text_centre_win (9, "finkel ruleset - compiled", CBYTE (4, 7), edge, right);
}

/* Clear the stage interior only - between the wings, never across them.
** rows_fill would wipe the full forty columns and take the curtains with
** it. */
static void stage_clear (unsigned char y0, unsigned char y1)
{
    unsigned char y;

    blit_ch = CH_SPACE;
    blit_cl = 0;
    for (y = y0; y <= y1; ++y) {
        blit_ptr = rowtab[y] + STAGE_LEFT;
        blit_run (STAGE_RIGHT - STAGE_LEFT + 1);
    }
}

/* The choices, with the whole block pushed `drop` rows down the stage.
** Lines that fall past the stage floor are simply not drawn, which is what
** makes this read as a scroll: at full drop only the first line is on, and
** the rest climb into view behind it. */
static void menu_items (unsigned char drop)
{
    unsigned char i, y;

    for (i = 0; i < MENU_ITEMS; ++i) {
        y = (unsigned char)(MENU_ROW0 + i + drop);
        if (y >= STAGE_BOT) continue;
        text_put (8, y, menu_item[i], CBYTE (7, 1));
    }
    y = (unsigned char)(MENU_MUTE_ROW + drop);
    if (y < STAGE_BOT) {
        ln_reset ();
        ln_str ("m  music: ");
        ln_str (bed_name[music_bed_now ()]);
        text_centre (y, ln_buf, CBYTE (3, 1));
    }

    y = (unsigned char)(MENU_SFX_ROW + drop);
    if (y < STAGE_BOT) {
        ln_reset ();
        ln_str ("s  effects: ");
        ln_str (music_sfx_on () ? "on" : "off");
        text_centre (y, ln_buf, CBYTE (3, 1));
    }

    /* The way out of a match, said on the way in.  It belongs here rather
    ** than in the chronicle at the start of every game: this is the screen
    ** somebody reads before they commit to a mode, and a line that only
    ** appears once the match has begun is a line nobody was looking at. */
    y = (unsigned char)(MENU_QUIT_ROW + drop);
    if (y < STAGE_BOT)
        text_centre (y, "x x x to leave game", CBYTE (3, 1));

    /* Which build this is, stamped by the Makefile.  Drawn HERE and not on
    ** the stage floor where it started: the menu scrolls in over rows 12
    ** to 22 and cleared it every time.  Anything that has to survive the
    ** scroll has to be redrawn by the thing doing the scrolling. */
    y = (unsigned char)(MENU_DATE_ROW + drop);
    if (y < STAGE_BOT)
        text_centre (y, "build " BUILD_DATE, CBYTE (2, 1));
}

/* Repaint just the music line, for when `m` has changed what it says.
** Clearing first, because the names are different lengths and a shorter
** one would otherwise leave the tail of a longer one behind it. */
static void menu_music_line (void)
{
    blit_ptr = rowtab[MENU_MUTE_ROW] + STAGE_LEFT;
    blit_ch  = CH_SPACE;
    blit_cl  = 0;
    blit_run (STAGE_RIGHT - STAGE_LEFT + 1);

    ln_reset ();
    ln_str ("m  music: ");
    ln_str (bed_name[music_bed_now ()]);
    text_centre (MENU_MUTE_ROW, ln_buf, CBYTE (3, 1));
}

/* Repaint just the effects line, for when `s` has changed what it says. */
static void menu_sfx_line (void)
{
    blit_ptr = rowtab[MENU_SFX_ROW] + STAGE_LEFT;
    blit_ch  = CH_SPACE;
    blit_cl  = 0;
    blit_run (STAGE_RIGHT - STAGE_LEFT + 1);

    ln_reset ();
    ln_str ("s  effects: ");
    ln_str (music_sfx_on () ? "on" : "off");
    text_centre (MENU_SFX_ROW, ln_buf, CBYTE (3, 1));
}

static void menu_scroll_in (void)
{
    unsigned char drop = (unsigned char)(STAGE_BOT - 1 - MENU_ROW0);

    for (;;) {
        stage_clear (MENU_ROW0, (unsigned char)(STAGE_BOT - 1));
        menu_items (drop);
        wait_frames_live (MENU_PACE);
        if (!drop) return;
        --drop;
    }
}

/* Arriving at the cabinet: the stage is dressed, the curtains are shut
** over it in a single frame so nothing is seen, and then they open on the
** fanfare.  Fourteen columns of travel at twelve frames each - the same
** three and a half seconds the fanfare lasts, because they are the same
** gesture. */
static unsigned char menu (void)
{
    unsigned char k, now, phase = 0;
    unsigned char lastf, shown;
    unsigned int  idle = 0;

    theatre_draw (CURTAIN_SHUT);
    music_resume ();
    curtain_open (menu_content);
    menu_scroll_in ();

    lastf = music_frames;
    shown = music_bed_now ();
    for (;;) {
        k = poll_key ();
        if (k) {
            if (keyis (k, 'm')) { music_bed_next (); menu_music_line (); k = 0; }
            else if (keyis (k, 's')) { music_sfx ((unsigned char)(!music_sfx_on ()));
                                       menu_sfx_line (); k = 0; }
            else if (k >= '1' && k <= '6') return (unsigned char)(k - '0');
            else if (k) sfx (SFX_ERROR);
            idle = 0;
        }

        /* A written bed hands over to the next when it ends, so the line
        ** can change without anybody touching a key. */
        if (music_bed_now () != shown) {
            shown = music_bed_now ();
            menu_music_line ();
        }

        lamps_frame (phase);
        ++phase;
        wait_frames_live (4);

        /* Counted in real frames off the interrupt's own counter rather
        ** than in loop iterations, so how long the chase takes to draw
        ** cannot change how long the cabinet waits. */
        now   = music_frames;
        idle += (unsigned char)(now - lastf);
        lastf = now;

        /* Left alone, the cabinet starts playing by itself rather than
        ** sitting on a prompt. */
        if (idle > ATTRACT_FRAMES) return MODE_DEMO;
    }
}

/* --- what you just chose ----------------------------------------------
**
** A menu that answers a keypress by simply vanishing gives the player no
** confirmation of WHICH thing they picked, and on a keyboard with no
** feedback of its own that matters more than it would elsewhere.  So the
** choice is read back: a chime, the mode's own name cut across the stage
** by the laser, a line saying what it is, and then a rising run under the
** closing curtain.
**
** The sequence is deliberately three sounds and three pictures rather than
** one of each - chime for "heard you", laser for "this is what you said",
** rise for "here it comes".  That is the shape of a machine acknowledging
** an instruction, and it is the whole of what a transition is for. */

static const char* const mode_name[MODE_RULES + 1] = {
    "", "vs urbot", "two players", "real lots", "urbot demo", "how we play"
};

static const char* const mode_line[MODE_RULES + 1] = {
    "",
    "you against the machine",
    "two at the keyboard",
    "two players, you throw the lots",
    "the machine plays itself",
    "the rules, and the path"
};

static void mode_screen (unsigned char m)
{
    if (m < 1 || m > MODE_RULES) return;

    /* The chime lands first and on its own, before anything moves: an
    ** acknowledgement that arrives with a picture is not an
    ** acknowledgement, it is part of the picture. */
    music_song (SONG_SELECT, 0);
    wait_frames_live (12);

    /* Clear the choices but keep the frame and the title - what is being
    ** replaced is the list, not the cabinet. */
    stage_clear (MENU_ROW0, (unsigned char)(STAGE_BOT - 1));
    wait_frames_live (8);

    etch_text (mode_name[m], MODE_NAME_ROW, ETCH_MODE);
    text_centre (MODE_LINE_ROW, mode_line[m], CBYTE (4, 1));
    wait_frames_live (MODE_HOLD);

    /* And the rise, under the curtain, which the caller closes next. */
    music_song (SONG_RISING, 0);
}

/* --- choosing a colour by seeing it ----------------------------------- */

static void colour_pick (unsigned char q)
{
    unsigned char hue, lum, i, k;

    for (;;) {
        TED_BGCOLOR = CBYTE (0, 0);
        screen_fill (CH_SPACE, 0);
        ln_reset (); ln_str ("colours for "); ln_str (names[q]);
        text_put (2, 0, ln_buf, CBYTE (7, 1));

        for (i = 0; i < 16; ++i) {
            unsigned char row = (unsigned char)(2 + (i & 7));
            unsigned char col = (unsigned char)(i < 8 ? 2 : 21);
            ln_reset ();
            ln_num ((unsigned char)(i + 1));
            ln_str (" ");
            ln_str (hue_name[i]);
            text_put (col, row, ln_buf, CBYTE (7, 1));
            blit_ptr = rowtab[row] + col + 12;
            blit_ch  = CH_SOLID;
            blit_cl  = CBYTE (5, i);
            blit_run (4);
        }

        /* The shade range is the program's, not a preference: outside it
        ** a piece either sinks into the tile it stands on or competes
        ** with the rosette gold.  See LUM_PICK_MIN in plus4.h. */
        text_put (2, 11, "shades run 3 to 6 - the range the", CBYTE (4, 1));
        text_put (2, 12, "board itself is lit in", CBYTE (4, 1));
        text_put (2, 13, "hue  1-16:", CBYTE (7, 1));
        hue = read_num (13, 13, 1, 16);
        text_put (2, 14, "shade 3-6:", CBYTE (7, 1));
        lum = read_num (13, 14, LUM_PICK_MIN, LUM_PICK_MAX);

        player_hue[q] = hue;
        player_lum[q] = lum;
        TED_BORDER = CBYTE (lum, (unsigned char)(hue - 1));

        /* The preview is drawn by the real renderers, not by an
        ** approximation of them - the same plaque routine the board uses,
        ** the same tile painter, and a rosette beside the piece so the
        ** colour can be judged against the gold it will sit on. */
        paint_button (5, 17, 107, 1, 0, 0);
        paint_button (11, 17, 87, (unsigned char)(CH_PIECE_BASE + 1), 1,
                      CBYTE (lum, (unsigned char)(hue - 1)));
        blit_ptr = rowtab[17] + 20;
        blit_ch  = CH_SOLID;
        blit_cl  = CBYTE (6, (unsigned char)(hue - 1));
        blit_run (11);
        text_put_rev ((unsigned char)(20 + ((11 - 5) >> 1)), 17, names[q],
                      CBYTE (6, (unsigned char)(hue - 1)));

        text_put (2, 22, "keep it?   y = yes   n = pick again", CBYTE (7, 1));
        for (;;) {
            k = wait_key ();
            if (keyis (k, 'y')) return;
            if (keyis (k, 'n')) break;
            sfx (SFX_ERROR);
        }
    }
}

/* --- the turn transition ---------------------------------------------- */

/* The background and border do not snap between players - they run a six
** step luminance gradient in the incoming player's hue while a six note
** cheer plays, so the ear knows whose turn begins without reading
** anything.  The background then settles back to true black, because
** every engraved element on this screen is reverse video. */
static void turn_sweep (unsigned char cp)
{
    unsigned char s;
    unsigned char hue = (unsigned char)(player_hue[cp] - 1);

    DBG_ENTER (DBG_GAM, "sweep");
    music_song (cp ? SONG_CHEER_B : SONG_CHEER_A, 0);
    /* SIX STEPS AT SIX FRAMES, and the six is not free to change on its
    ** own: the cheer is six notes, so the flare and the music are the same
    ** gesture and have to be the same length.  Both were twice this and
    ** both were halved together - a hand-over between players happens
    ** constantly, and a second and a half of it every time is most of what
    ** made a match feel slow. */
    for (s = 0; s < 6; ++s) {
        TED_BGCOLOR = CBYTE ((unsigned char)(7 - s), hue);
        TED_BORDER  = CBYTE ((unsigned char)(s + 2), hue);
        wait_frames_live (SWEEP_PACE);
    }
    TED_BGCOLOR = CBYTE (0, 0);
    TED_BORDER  = CBYTE (player_lum[cp], hue);
    DBG_LEAVE ();
}

/* --- moving a piece --------------------------------------------------- */

/*
** The travelling flash.
**
** A piece does not simply appear on the next square.  As it arrives, the
** square it has just left goes dark, flashes white and comes back to
** exactly the colour it was; and at the same time the square it is
** arriving at flashes white and settles into the colour of the player who
** now holds it.  The two run together, so what the eye sees is one flash
** moving along the path with the piece inside it.
**
** Three kinds of square, and the difference between them is the whole
** point - the animation says what happened, not just that something did:
**
**   PASS    a square being vacated, or merely jumped over on the way
**           past.  Black, white, and back to its OWN colour.  Nothing
**           about it changed hands, so nothing about it ends up different.
**   CLAIM   the square being landed on.  White flash, then the mover's
**           colour and hue: it now belongs to them.
**   BATTLE  the same, but a foe was standing there.  Red, white, red,
**           black, and then the winner's colour - a fight resolving,
**           rather than a handover.
**
** Each is a function of the frame number, so the sequences are code
** rather than tables: they are short, they are read once per frame, and
** written this way the shape of each one is legible at a glance.
*/

#define FLASH_LEN       8               /* stages in a sequence           */
#define FLASH_PACE      1               /* frames a stage, in transit     */
#define FLASH_LAND      3               /* ...and on the square landed on */

/* Vacated, or jumped over: down to black, a white flash, and back to
** where it started. */
static unsigned char seq_pass (unsigned char f, unsigned char b)
{
    unsigned char l = (unsigned char)(b >> 4);
    unsigned char h = (unsigned char)(b & 0x0F);

    switch (f) {
    case 0:  return b;
    case 1:  return CBYTE (l >> 1, h);
    case 2:  return CBYTE (0, 0);                       /* black          */
    case 3:  return CBYTE (7, 1);                       /* the flash      */
    case 4:  return CBYTE (6, 1);
    case 5:  return CBYTE (l >= 2 ? (unsigned char)(l - 2) : 0, h);
    default: return b;                                  /* its own colour */
    }
}

/* Landed on: a white flash that resolves into the mover's own colour. */
static unsigned char seq_claim (unsigned char f, unsigned char m)
{
    unsigned char l = (unsigned char)(m >> 4);
    unsigned char h = (unsigned char)(m & 0x0F);

    switch (f) {
    case 0:
    case 1:  return CBYTE (7, 1);
    case 2:  return CBYTE (6, 1);
    case 3:  return CBYTE (l < 7 ? (unsigned char)(l + 1) : 7, h);
    default: return m;
    }
}

/* A capture.  Red, white, red, black, white - and out of it comes the
** winner. */
static unsigned char seq_battle (unsigned char f, unsigned char m)
{
    switch (f) {
    case 0:  return CBYTE (6, 2);                       /* red            */
    case 1:  return CBYTE (7, 1);
    case 2:  return CBYTE (5, 2);
    case 3:  return CBYTE (0, 0);
    case 4:  return CBYTE (7, 1);
    default: return m;
    }
}

/* Run both cells' sequences together for one step of the path.  Either
** square may be absent - entering from the pool has nothing behind it and
** bearing off has nothing in front - and 0 means "no square". */
static void flash_pair (unsigned char cp, unsigned char prev,
                        unsigned char next, unsigned char claim,
                        unsigned char battle, unsigned char pace)
{
    unsigned char f, pb = 0, nb = 0;
    unsigned char m = CBYTE (player_lum[cp],
                             (unsigned char)(player_hue[cp] - 1));

    if (prev && prev <= PATH_LAST) pb = sq_c[cp][prev];
    if (next && next <= PATH_LAST) nb = sq_c[cp][next];

    for (f = 0; f < FLASH_LEN; ++f) {
        if (pb) square_tint (cp, prev, seq_pass (f, pb), 0);
        if (nb) {
            if (!claim)       square_tint (cp, next, seq_pass (f, nb), 0);
            else if (battle)  square_tint (cp, next, seq_battle (f, m), 0);
            else              square_tint (cp, next, seq_claim (f, m), 0);
        }
        wait_frames_live (pace);
    }

    /* Whatever the sequence did, the board has the last word. */
    if (pb) draw_cell (cp, prev);
    if (nb) draw_cell (cp, next);
}

/* Motion tweening.  The frozen edition drew the piece on each intermediate
** board SQUARE with a delay between; a square is 32 POKEs, so anything
** finer was unaffordable.  Here the glyph travels one CHARACTER cell at a
** time along the line between squares - five steps where there was one -
** and the piece reads as travelling rather than teleporting. */
/* Where a player's waiting pieces sit, on their own plaque.
**
** This was `cp ? 9 : 2` written out twice, which was right for a twelve
** row board and wrong the moment the black separators pushed the lower
** band down: row 9 became a separator, so player two's pieces flew to and
** from a black gap between the bands.  It has to agree with
** plaque_tokens, which is why it is one function and not two literals. */
static unsigned char pool_row (unsigned char cp)
{
    return (unsigned char)(band_y[cp * 2] + (cp ? 1 : 2));
}

static void animate_move (unsigned char cp, unsigned char mi,
                          unsigned char from, unsigned char to)
{
    unsigned char glyph = (unsigned char)(CH_PIECE_BASE + mi);
    unsigned char col   = CBYTE (player_lum[cp],
                                 (unsigned char)(player_hue[cp] - 1));
    unsigned char keep  = piece[cp][mi];
    unsigned char t, sx, sy, dx, dy, prev;

    DBG_ENTER (DBG_GAM, "move");
    DBG_VAL2 (DBG_GAM, "sq", from, to);

    /* 255 is "in flight": it matches no square, so nothing draws the piece
    ** at either end while it is between them.  The frozen edition used -1
    ** for the same trick. */
    piece[cp][mi] = 255;
    if (from) draw_cell (cp, from);

    if (from) { sx = piece_cell_x (cp, from); sy = piece_cell_y (cp, from); }
    else      { sx = 25; sy = pool_row (cp); }              /* from the waist */

    prev = from;
    for (t = (unsigned char)(from + 1); t <= to; ++t) {
        if (t > PATH_LAST) {                    /* bearing off */
            dx = 25;
            dy = pool_row (cp);
        } else {
            dx = piece_cell_x (cp, t);
            dy = piece_cell_y (cp, t);
        }
        glide (sx, sy, dx, dy, glyph, col, 1);
        sfx (SFX_STEP);
        sx = dx;
        sy = dy;

        /* The square just left and the square just reached, together.
        ** Only the last one is claimed - the rest are jumped over, and a
        ** jumped square keeps its colour because nothing about it
        ** changed.  The landing gets three frames a stage where transit
        ** gets one, so the end of a move has weight. */
        flash_pair (cp, prev, (unsigned char)(t <= PATH_LAST ? t : 0),
                    (unsigned char)(t == to), mv_captures,
                    (unsigned char)(t == to ? FLASH_LAND : FLASH_PACE));
        prev = (unsigned char)(t <= PATH_LAST ? t : 0);
        music_service ();
    }

    piece[cp][mi] = keep;
    if (to <= PATH_LAST) draw_cell (cp, to);
    paint_tokens ();
    DBG_LEAVE ();
}

/* --- leaving a match early -------------------------------------------
**
** X THREE TIMES, and nothing else.  One way out of every match, the same
** in all four modes, whether a human is playing or being watched.
**
** Abandoning a game somebody is in the middle of is not something to do on
** one stray keypress, and this is a keyboard with no modifiers to hide
** behind, so the guard is repetition.  Each press says where it has got
** to, because a key that appears to do nothing twice reads as a broken key
** rather than as a deliberate one - and any other key in between puts the
** count back to nought, so it has to be three in a row and meant. */

#define QUIT_X_NEEDED   3

static unsigned char quit_x;            /* x presses in a row             */
static unsigned char demo_said;         /* turns since the demo said how  */
static unsigned char abandoning;        /* the match should end now       */

/* Every key a match sees goes through here first.  Returns 1 if the key
** was consumed and the caller should not treat it as its own. */
static unsigned char quit_key (unsigned char k)
{
    if (!k) return 0;

    if (keyis (k, 'x')) {
        ++quit_x;
        if (quit_x >= QUIT_X_NEEDED) {
            quit_x     = 0;
            abandoning = 1;
            say ("leaving the game");
        } else {
            ln_reset ();
            ln_str ("x ");
            ln_num (quit_x);
            ln_str (" of ");
            ln_num (QUIT_X_NEEDED);
            ln_str (" - again to leave the game");
            say_line ();
        }
        return 1;
    }

    /* Any other key breaks the run, so three has to mean three in a row. */
    quit_x = 0;
    return 0;
}

/* Polled from the turn loop, which is the only place a bot's turn can be
** interrupted at all - nobody is being prompted during one. */
static unsigned char check_quit (void)
{
    unsigned char k;

    if (abandoning) return 1;
    k = poll_key ();
    if (quit_key (k)) return abandoning;
    if (!k) return 0;
    /* SPACE leaves a demo, and only a demo. Nobody is playing one, so
    ** there is nothing to protect and no reason to make a spectator press
    ** a key three times to stop watching. In every other mode space means
    ** nothing here and x-three-times is the way out. */
    if (k == ' ' && mode == MODE_DEMO) { abandoning = 1; return 1; }
    if (keyis (k, 'm')) music_bed_next ();
    else                sfx_key (k);
    return 0;
}

/* --- a turn ----------------------------------------------------------- */

static unsigned char human_choose (unsigned char cp)
{
    unsigned char j, k;

    ln_reset ();
    ln_str (names[cp]);
    ln_str (" - pick a piece, h = off");
    say_line ();

    ln_reset ();
    for (j = 1; j <= PIECES; ++j) {
        if (!legal[j]) continue;
        ln_num (j);
        ln_ch ('>');
        if (legal[j] == SQ_HOME) ln_ch ('h');
        else                     ln_num (legal[j]);
        ln_ch (' ');
    }
    say_line ();

    for (;;) {
        k = wait_key ();
        /* x-three-times has to reach the player who is actually playing,
        ** so it is tested here as well as in the turn loop.  Returning a
        ** piece nobody asked for is safe: play checks `abandoning` before
        ** it uses this. */
        if (quit_key (k)) { if (abandoning) return 0; continue; }
        if (keyis (k, 'm')) { music_bed_next (); continue; }
        if (sfx_key (k)) continue;
        if (k < '1' || k > '7') { sfx (SFX_ERROR); continue; }
        j = (unsigned char)(k - '0');
        if (!legal[j]) { sfx (SFX_ERROR); continue; }
        return j;
    }
}

static unsigned char manual_roll (unsigned char cp)
{
    unsigned char k;

    ln_reset ();
    ln_str (names[cp]);
    ln_str (" - type the tip count 0-4");
    say_line ();

    for (;;) {
        k = wait_key ();
        if (quit_key (k)) { if (abandoning) return 0; continue; }
        if (sfx_key (k)) continue;
        if (k >= '0' && k <= '4') return (unsigned char)(k - '0');
        /* The bed cycles from here too.  A manual match spends half its
        ** prompts at this one, and `m` working at the piece prompt but
        ** being refused at the roll prompt is the kind of inconsistency
        ** that reads as the key having stopped working. */
        if (keyis (k, 'm')) { music_bed_next (); continue; }
        sfx (SFX_ERROR);
    }
}

/* The end of a match, in the order the frozen edition does it: the border
** flares, the theme starts, the board and the score are KEPT - a winner
** should be able to look at the position that won - and the cup is poured
** over the casting floor with the winner's name cut into its bowl.
**
** Returns 1 if the same two players want another match. */
static unsigned char victory (unsigned char cp)
{
    unsigned char i, k;

    music_song (SONG_THEME, 0);
    for (i = 0; i < 10; ++i) {
        TED_BORDER = CBYTE (7, (unsigned char)(rnd_below (15) + 1));
        wait_frames_live (6);
    }
    TED_BORDER = CBYTE (player_lum[cp], (unsigned char)(player_hue[cp] - 1));

    /* The match is over, so nobody is waiting their turn: both sides come
    ** back to full colour for the board the winner gets to look at. */
    player_dim[0] = player_dim[1] = 0;
    board_relight ();

    rows_fill (BOARD_ROWS, 24, CH_SPACE, 0);   /* everything below the board */
    ln_reset ();
    ln_str (names[cp]);
    ln_str (" wins the royal game of ur");
    text_centre (13, ln_buf, CBYTE (7, 7));

    ln_reset ();
    ln_str ("the other side got ");
    ln_num (count_at_home ((unsigned char)(cp ^ 1)));
    ln_str (" home");
    text_centre (14, ln_buf, CBYTE (5, 7));

    /* THE NAME AT THE SIZE OF THE SCREEN.  Block letters at a five column
    ** pitch, so eight characters - which is the length of the name field,
    ** and not a coincidence - span thirty-nine of the forty columns.  It
    ** was cut into one row before, eight cells of the eleven under the
    ** board, and a winner's name deserves more of the screen than a throw
    ** of the dice gets.
    **
    ** Then it is taken away again, because the cup goes where it stood and
    ** the two of them fighting over rows 16-20 is the one arrangement
    ** worse than either alone. */
    etch_word (names[cp], BIG_WIN_ROW, ETCH_WIN);
    wait_frames_live (WIN_HOLD);
    rows_fill (APRON_TOP + 1, APRON_BOT, CH_SPACE, 0);   /* the two lines stay */

    trophy_draw (names[cp]);
    /* And the celebration over the top of it.  The name is already cut and
    ** the cup already poured, so this adds nothing to what the player
    ** knows - which is exactly what a celebration is. */
    fireworks (FIREWORK_LEN);

    /* A DEMO HAS NOBODY TO PRESS THE KEY.
    **
    ** This waited on space in every mode, and the check for a demo came
    ** AFTER the wait - so an unattended attract loop played a whole match,
    ** poured the trophy, and then stopped dead on `press space` for ever.
    ** Found by a soak run: at 1.8e9 and 2.1e9 cycles, three hundred
    ** million apart, the only cells on screen that had changed were the
    ** tiles' highlight rows, which is the shimmer running over a game that
    ** had finished.
    **
    ** So the demo reads its own cup for a few seconds and then goes back
    ** to the cabinet by itself, which is what an attract mode is for. A
    ** human still gets to look for as long as they like. */
    if (mode == MODE_DEMO) {
        wait_frames_live (DEMO_CUP_HOLD);
        rows_fill (24, 24, CH_SPACE, 0);
        return 0;                       /* a demo returns to the cabinet */
    }

    text_centre (24, "press space", CBYTE (4, 1));
    while (wait_key () != ' ') ;

    rows_fill (24, 24, CH_SPACE, 0);

    text_centre (24, "play again?   y = yes   n = no", CBYTE (7, 1));
    for (;;) {
        k = wait_key ();
        if (keyis (k, 'y')) return 1;
        if (keyis (k, 'n')) return 0;
        sfx (SFX_ERROR);
    }
}

/* `replay` keeps the names and the colours from the last match, which is
** what "play again" means - the same two people, a fresh board. */
static void setup (unsigned char replay)
{
    unsigned char i;

    reset_board ();
    urbot_new_match ();
    chronicle_reset ();
    player_dim[0] = player_dim[1] = 0;
    if (replay) return;

    if (mode == MODE_DEMO) {
        for (i = 0; i < 8; ++i) {
            names[0][i] = "urbot a"[i];
            names[1][i] = "urbot b"[i];
        }
        player_hue[0] = 9; player_lum[0] = 6;
        player_hue[1] = 7; player_lum[1] = 5;
        player_name[0] = names[0];
        player_name[1] = names[1];
        return;
    }

    TED_BGCOLOR = CBYTE (0, 0);
    screen_fill (CH_SPACE, 0);
    text_put (2, 2, "player one, your name:", CBYTE (7, 1));
    input_line (2, 4, names[0], 8);
    if (!names[0][0]) { names[0][0] = 'e'; names[0][1] = 'n';
                        names[0][2] = 'a'; names[0][3] = 0; }

    if (mode == MODE_VS_BOT) {
        names[1][0] = 'u'; names[1][1] = 'r'; names[1][2] = 'b';
        names[1][3] = 'o'; names[1][4] = 't'; names[1][5] = 0;
    } else {
        text_put (2, 6, "player two, your name:", CBYTE (7, 1));
        input_line (2, 8, names[1], 8);
        if (!names[1][0]) { names[1][0] = 'd'; names[1][1] = 'u';
                            names[1][2] = 'm'; names[1][3] = 'u';
                            names[1][4] = 'z'; names[1][5] = 'i';
                            names[1][6] = 0; }
    }
    player_name[0] = names[0];
    player_name[1] = names[1];

    colour_pick (0);
    if (mode == MODE_VS_BOT) {
        player_hue[1] = (unsigned char)(player_hue[0] + 7);
        if (player_hue[1] > 16) player_hue[1] -= 15;
        if (player_hue[1] < 2)  player_hue[1] = 2;
        player_lum[1] = LUM_PICK_MAX;   /* URBOT obeys the same band */
    } else {
        colour_pick (1);
    }
}

/* The opening ceremony: four lots each, higher hand opens, ties re-thrown.
** It uses the real lots rather than a uniform 0-4, which the frozen
** edition got wrong once and recorded as backlog item 14.1. */
static void opening_throw (void)
{
    unsigned char a, b;

    say ("the opening throw - higher hand opens");
    for (;;) {
        thrower_colour = CBYTE (player_lum[0],
                                (unsigned char)(player_hue[0] - 1));
        a = throw_lots ();
        ln_reset (); ln_str (names[0]); ln_str (" counts "); ln_num (a);
        ln_str (" tips"); say_line ();
        wait_frames_live (BEAT_NOTE);
        floor_wipe ();

        thrower_colour = CBYTE (player_lum[1],
                                (unsigned char)(player_hue[1] - 1));
        b = throw_lots ();
        ln_reset (); ln_str (names[1]); ln_str (" counts "); ln_num (b);
        ln_str (" tips"); say_line ();
        wait_frames_live (BEAT_NOTE);
        floor_wipe ();

        if (a != b) break;
        say ("a tie - the lots are cast again");
        wait_frames_live (BEAT_NOTE);
    }
    turn = (unsigned char)(b > a);
    ln_reset (); ln_str (names[turn]); ln_str (" throws higher and opens");
    say_line ();
    wait_frames_live (BEAT_NOTE);
    floor_wipe ();
}

/* One match.  Returns 1 when the same players want another. */
static unsigned char play (unsigned char replay)
{
    unsigned char roll, pick, from, to;

    setup (replay);
    board_draw ();
    music_resume ();
    quit_x     = 0;
    abandoning = 0;

    /* A DEMO IS WATCHED, NOT PLAYED, and the two want different sound.
    ** Nobody is throwing these lots, so the clicks and thuds are reporting
    ** on somebody else's turn - which over an unattended attract loop is
    ** just noise. The music stays: that is the thing worth leaving on.
    ** The `s` key overrides this either way, and the setting is put back
    ** when the demo ends so it does not leak into a real match. */
    if (mode == MODE_DEMO) {
        music_sfx (0);
        say ("demo - press space, or x x x");
    } else {
        music_sfx (1);
    }


    last_turn = 255;
    opening_throw ();

    for (;;) {
        /* One turn is one unit of the trace: the depth column is reset
        ** here so a missed dbg_leave cannot make it drift all match. */
        DBG_DEPTH0 ();
        DBG_ENTER (DBG_GAM, "turn");
        DBG_VAL (DBG_GAM, "who", turn);
        if (check_quit ()) return 0;

        if (turn != last_turn) turn_sweep (turn);
        last_turn = turn;

        /* Said again every eighth turn. The chronicle is four lines and
        ** scrolls, so a line said once at the start of an attract loop is
        ** gone long before anybody wanders past and wonders how to stop
        ** it. Eight turns is far enough apart not to crowd URBOT out of
        ** its own deliberation. */
        if (mode == MODE_DEMO && ++demo_said >= 8) {
            demo_said = 0;
            say ("demo - press space, or x x x");
        }

        /* Whose turn it is, said in light rather than in words: the other
        ** side's four rows of squares and their waist plaque recede three
        ** luminance steps, and the shimmer stops crossing them.  Set every
        ** turn rather than only on a change, because a rosette gives the
        ** same player another throw and the board should not have to know
        ** the difference. */
        player_dim[turn]     = 0;
        player_dim[turn ^ 1] = 1;
        board_relight ();
        TED_BORDER = CBYTE (player_lum[turn],
                            (unsigned char)(player_hue[turn] - 1));
        thrower_colour = CBYTE (player_lum[turn],
                                (unsigned char)(player_hue[turn] - 1));
        floor_wipe ();

        ln_reset (); ln_str (names[turn]); ln_str (" takes the turn");
        say_line ();

        if (mode == MODE_MANUAL) {
            roll = manual_roll (turn);
            if (abandoning) return 0;   /* x-three-times, at the prompt */
            lots_show (roll);
        } else {
            roll = throw_lots ();
        }
        sfx (SFX_THROW);
        ln_reset (); ln_str (names[turn]); ln_str (" counts "); ln_num (roll);
        ln_str (" tips"); say_line ();

        DBG_VAL (DBG_GAM, "roll", roll);
        find_legal_moves (turn, roll);
        DBG_VAL (DBG_RUL, "legal", legal_count);
        if (check_quit ()) return 0;

        if (roll == 0) {
            say ("no tips showing - the turn is lost");
            wait_frames_live (BEAT_READ);
            turn ^= 1;
            continue;
        }
        if (legal_count == 0) {
            say ("no legal move - the turn is lost");
            wait_frames_live (BEAT_READ);
            turn ^= 1;
            continue;
        }

        if (mode == MODE_DEMO || (mode == MODE_VS_BOT && turn == 1)) {
            DBG_ENTER (DBG_BOT, "think");
            pick = urbot_choose (turn);
            DBG_LEAVE ();
        } else {
            pick = human_choose (turn);
            if (abandoning) return 0;   /* x-three-times, at the prompt */
        }
        DBG_VAL (DBG_GAM, "pick", pick);

        DBG_BOUND (DBG_GAM, "pick", pick, PIECES + 1);
        from = piece[turn][pick];
        to   = legal[pick];
        DBG_BOUND (DBG_GAM, "from", from, SQ_HOME + 1);
        DBG_BOUND (DBG_GAM, "to", to, SQ_HOME + 1);
        execute_move (turn, pick);
        animate_move (turn, pick, from, to);

        if (mv_captures) {
            sfx (SFX_CAPTURE);
            /* BURNT, not cut.  The laser makes a word out of nothing,
            ** which is right for a title and wrong for this: a capture is
            ** something that WAS there going away, and fire is the effect
            ** that says so.
            **
            ** It takes the whole apron - the casting floor AND the four
            ** lines of the log.  The floor is empty between throws and the
            ** log can repaint itself from its own buffer, so the most
            ** violent thing in the game costs nothing except the time,
            ** which is the one thing it should cost. */
            burn_apron ("captured", BURN_CAPTURE);
            wait_frames_live (BEAT_NOTE);       /* the word left standing  */
            apron_clear ();
            ln_reset (); ln_str (names[turn]); ln_str (" captures a foe on ");
            ln_num (to); say_line ();
        }
        if (to == SQ_HOME) {
            sfx (SFX_HOME);
            etch_text ("home", ETCH_ROW, ETCH_HOME);
            floor_wipe ();
            ln_reset (); ln_str ("piece "); ln_num (pick); ln_str (" of ");
            ln_str (names[turn]); ln_str (" is home"); say_line ();
        }

        if (mv_won) { DBG_SAY (DBG_GAM, "won"); return victory (turn); }
        if (check_quit ()) return 0;

        if (mv_rosette) {
            sfx (SFX_ROSETTE);
            ln_reset (); ln_str ("a rosette - "); ln_str (names[turn]);
            ln_str (" throws again"); say_line ();
            wait_frames_live (BEAT_NOTE);
            continue;                   /* the same player, no sweep */
        }
        turn ^= 1;
        DBG_LEAVE ();
    }
}

/* --- how we play ------------------------------------------------------ */

static const char* const rule_text[10] = {
    "a walks 1-14 and b mirrors it.",
    "four pyramid lots, two tipped corners",
    "each. count the tips: 0 to 4 steps.",
    "a nil throw loses the turn. nil and 4",
    "are one in sixteen. no doubles exist.",
    "a piece enters on the square equal to",
    "the throw - a 4 enters on rosette 4.",
    "5 to 12 is shared: land on a foe and",
    "it is sent back to the start. 4, 8 and",
    "14 are rosettes: safe, one more throw."
};

static void rules_screen (void)
{
    unsigned char s, i, well;

    TED_BGCOLOR = CBYTE (0, 0);
    screen_fill (CH_SPACE, 0);
    paint_table ();
    paint_buttons ();

    /* The path numbered 1 to 14 in the squares themselves, which answers
    ** the one question the board cannot: which way the pieces walk. */
    for (s = 1; s <= PATH_LAST; ++s) {
        well = (unsigned char)(s < 10 ? CH_PIECE_BASE + s : CH_PIECE_BASE + 1);
        paint_button (sq_x[0][s], sq_y[0][s], sq_c[0][s], well, 0, CBYTE (7, 1));
        if (s >= 10) {
            *(rowtab[sq_y[0][s] + 1] + sq_x[0][s] + 2) =
                (unsigned char)(CH_PIECE_BASE + s - 10);
            *(rowtab[sq_y[0][s] + 1] + sq_x[0][s] + 2 - SCREEN_COLOR_D) =
                CBYTE (7, 1);
        }
    }

    rows_fill (BOARD_ROWS, BOARD_ROWS, CH_SOLID, CBYTE (5, 7));
    text_put_rev (3, 12, "how we play          space = back", CBYTE (5, 7));
    for (i = 0; i < 10; ++i)
        text_put (1, (unsigned char)(13 + i), rule_text[i], CBYTE (6, 1));

    text_put (1, 24, "bearing off is exact: 14 needs a 1.", CBYTE (6, 1));
    while (wait_key () != ' ') ;
}

/* --- boot ------------------------------------------------------------- */

void main (void)
{
    unsigned char again = 0;

    cursor (0);                         /* no terminal cursor on a board */
    board_init ();
    kbd_init ();                        /* learn the idle matrix, once   */

    /* The game plays in the soft shading profile.  board_init leaves the
    ** legacy one in place so that demo.c - the conformance oracle - draws
    ** the frozen edition's board without having to ask for anything, and
    ** the divergence is stated here, at the single place that wants it. */
    shade_profile (SHADE_SOFT);

    /* Constructor injection, such as C has one: the trace is handed the
    ** sink it should write to, at the one point in the program that knows
    ** what kind of run this is.  Swap dbg_screen_sink for dbg_null_sink
    ** and the breadcrumb is still kept but nothing is drawn - which is
    ** what a headless run wants.  In a production build this line does not
    ** exist at all. */
#ifdef DEBUG
    dbg_init (&dbg_screen_sink);
#endif
    music_init ();
    rnd_seed (0xACE1U);

    /* THE WAY IN, once.  Trumpets over a laser cutting the two letters,
    ** and then the cabinet.  It is here rather than inside menu() because
    ** menu() is returned to after every match and an entrance that happens
    ** twice is not an entrance. */
    TED_BGCOLOR = CBYTE (0, 0);
    TED_BORDER  = CBYTE (0, 0);
    screen_fill (CH_SPACE, 0);
    music_song (SONG_INTRO, 0);
    etch_title (ETCH_INTRO);
    wait_frames_live (40);              /* the last chime, over the letters */

    for (;;) {
        mode = menu ();
        rnd_stir ();
        if (mode > MODE_RULES) continue;        /* 6 is reserved for the
                                                ** on-target self-test  */

        /* Read the choice back before acting on it. */
        mode_screen (mode);

        /* Leaving the cabinet: the curtains come across before the scene
        ** behind them changes, so the board never appears by cutting. */
        curtain_close ();

        if (mode == MODE_RULES) { rules_screen (); continue; }

        /* "Play again" keeps the same two players and their colours, so
        ** the rematch starts on a fresh board and nothing else. */
        while (play (again)) again = 1;
        again = 0;
    }
}
