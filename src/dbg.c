/* ------------------------------------------------------------------------
 * dbg.c - the trace facility.  See dbg.h for the design and the keys.
 *
 * The entire file is inside #ifdef DEBUG.  In a production build this is
 * an empty translation unit, which is the point: the cost of the facility
 * when it is switched off has to be zero bytes and zero cycles, not "a
 * branch that is usually not taken".
 * --------------------------------------------------------------------- */

#include "dbg.h"

#ifdef DEBUG

#include "plus4.h"
#include "board.h"

#ifdef __CC65__
#  include <conio.h>
#endif

extern unsigned char scr_code (unsigned char c);

/* --- geometry --------------------------------------------------------- */
/* The overlay lives in the right-hand 24 columns of the bottom half, so
** the board above it stays readable while a match is being traced.  It is
** deliberately not the chronicle's rows alone: the chronicle is what the
** GAME says, and this is what the PROGRAM says, and confusing the two is
** how a trace ends up written for the player instead of the author. */

#define DBG_LEFT        16
#define DBG_WIDTH       24
#define DBG_TOP         13
#define DBG_LINES       12

static char          ring[DBG_LINES][DBG_WIDTH + 1];
static unsigned char ring_n;            /* how many lines are in it      */
static unsigned char overlay = 1;       /* the debug build starts loud   */
static unsigned char filter = DBG_SYS_COUNT;    /* == count means all    */
static unsigned char stepping;

static const struct dbg_sink* sink;

/* Three letters each.  Index by the DBG_* constants in dbg.h. */
static const char* const dbg_tag[DBG_SYS_COUNT] = {
    "gam", "brd", "rul", "dic", "bot", "mus", "frt", "txt"
};

/* --- the shadow return stack ------------------------------------------ */
/* Eight deep, which is more than this program nests.  An overflow does not
** grow the array, it stops recording: a trace that lies about its depth is
** worse than one that admits it ran out. */

#define DBG_MAXDEPTH    8

static const char*  crumb[DBG_MAXDEPTH];
static unsigned char crumb_sys[DBG_MAXDEPTH];
static unsigned char depth;

/* --- line building ---------------------------------------------------- */
/* Its own formatter, on purpose.  Borrowing text.c's line builder would
** mean the chronicle's state changed whenever the program was traced, and
** a probe that perturbs what it measures is not a probe. */

static char          buf[DBG_WIDTH + 1];
static unsigned char blen;

static void b_reset (void)
{
    blen = 0;
    buf[0] = 0;
}

static void b_ch (char c)
{
    if (blen >= DBG_WIDTH) return;
    buf[blen++] = c;
    buf[blen]   = 0;
}

static void b_str (const char* s)
{
    while (*s) b_ch (*s++);
}

static void b_num (unsigned int v)
{
    unsigned int d = 10000;
    unsigned char started = 0;

    if (!v) { b_ch ('0'); return; }
    while (d) {
        unsigned char k = (unsigned char)(v / d);
        if (k || started) { b_ch ((char)('0' + k)); started = 1; }
        v %= d;
        d /= 10;
    }
}

/* --- the screen sink -------------------------------------------------- */

static void screen_open (void)
{
    unsigned char y, x;

    for (y = 0; y < DBG_LINES; ++y)
        for (x = 0; x < DBG_WIDTH; ++x) {
            rowtab[DBG_TOP + y][DBG_LEFT + x] = CH_SPACE;
            *(rowtab[DBG_TOP + y] + DBG_LEFT + x - SCREEN_COLOR_D) = 0;
        }
}

/* Repaint the whole panel.  Twelve lines of twenty-four is 288 bytes a
** frame at worst, which on a compiled machine is nothing and on the
** interpreted one would have been half a second - a reminder of why the
** BASIC edition could not have had this at all. */
static void screen_paint (void)
{
    unsigned char y, x;
    const char*   s;
    unsigned char col;

    for (y = 0; y < DBG_LINES; ++y) {
        s   = ring[y];
        /* The newest line is brightest; older ones fade, so the eye finds
        ** the bottom of the log without being told where it is. */
        col = CBYTE ((unsigned char)(y >= DBG_LINES - 2 ? 7
                                     : (y >= DBG_LINES - 5 ? 5 : 3)), 1);
        for (x = 0; x < DBG_WIDTH; ++x) {
            rowtab[DBG_TOP + y][DBG_LEFT + x] =
                s[x] ? scr_code ((unsigned char)s[x]) : CH_SPACE;
            *(rowtab[DBG_TOP + y] + DBG_LEFT + x - SCREEN_COLOR_D) = col;
        }
    }
}

static void screen_line (const char* s)
{
    unsigned char i, j;

    for (i = 0; i < DBG_LINES - 1; ++i)          /* scroll up */
        for (j = 0; j <= DBG_WIDTH; ++j)
            ring[i][j] = ring[i + 1][j];

    j = 0;
    while (j < DBG_WIDTH && s[j]) { ring[DBG_LINES - 1][j] = s[j]; ++j; }
    while (j < DBG_WIDTH)         { ring[DBG_LINES - 1][j] = ' ';  ++j; }
    ring[DBG_LINES - 1][DBG_WIDTH] = 0;
    if (ring_n < DBG_LINES) ++ring_n;

    if (overlay) screen_paint ();
}

const struct dbg_sink dbg_screen_sink = { screen_line, screen_open };

/* --- the null sink ---------------------------------------------------- */
/* Draws nothing, so a headless run costs only the formatting.  The
** breadcrumb is still kept, which is the part worth having when the thing
** you are debugging is a machine that has stopped. */

static void null_line (const char* s)   { (void)s; }
static void null_open (void)            { }

const struct dbg_sink dbg_null_sink = { null_line, null_open };

/* --- emitting --------------------------------------------------------- */

static unsigned char wanted (unsigned char sys)
{
    return (unsigned char)(filter >= DBG_SYS_COUNT || filter == sys);
}

/* Every line starts the same way: depth, then the three letter tag.  Two
** fixed columns mean a person can filter with their eyes before reaching
** for the filter key. */
static void b_head (unsigned char sys)
{
    b_reset ();
    b_ch ((char)('0' + (depth <= 9 ? depth : 9)));
    b_str (sys < DBG_SYS_COUNT ? dbg_tag[sys] : (const char*)"???");
    b_ch (' ');
}

static void emit (void)
{
    sink->line (buf);

#ifdef __CC65__
    /* Step mode: hold on every line until a key.  f4 again lets go. */
    while (stepping) {
        unsigned char k = cgetc ();
        if (k == DBG_K_STEP) { stepping = 0; break; }
        if (k) break;
    }
#endif
}

void dbg_init (const struct dbg_sink* s)
{
    unsigned char i, j;

    sink = s ? s : &dbg_null_sink;
    for (i = 0; i < DBG_LINES; ++i)
        for (j = 0; j <= DBG_WIDTH; ++j)
            ring[i][j] = 0;
    ring_n = 0;
    depth  = 0;
    sink->open ();
    dbg_say (DBG_GAM, "trace on - f1 f2 f3 f4");
}

void dbg_enter (unsigned char sys, const char* name)
{
    if (depth < DBG_MAXDEPTH) {
        crumb[depth]     = name;
        crumb_sys[depth] = sys;
    }
    ++depth;
    if (!wanted (sys)) return;
    b_head (sys);
    b_ch ('>');
    b_str (name);
    emit ();
}

void dbg_leave (void)
{
    if (depth) --depth;
}

void dbg_say (unsigned char sys, const char* msg)
{
    if (!wanted (sys)) return;
    b_head (sys);
    b_str (msg);
    emit ();
}

void dbg_val (unsigned char sys, const char* name, unsigned int v)
{
    if (!wanted (sys)) return;
    b_head (sys);
    b_str (name);
    b_ch ('=');
    b_num (v);
    emit ();
}

void dbg_val2 (unsigned char sys, const char* name,
               unsigned int a, unsigned int b)
{
    if (!wanted (sys)) return;
    b_head (sys);
    b_str (name);
    b_ch ('=');
    b_num (a);
    b_ch (',');
    b_num (b);
    emit ();
}

/* v must be below lim.  When it is not, say so loudly and once: the first
** out-of-range index is the interesting one, and everything after it is
** happening in a program whose memory has already been changed. */
void dbg_bound (unsigned char sys, const char* name,
                unsigned int v, unsigned int lim)
{
    static unsigned char latched;

    if (v < lim) return;
    if (latched) return;
    latched = 1;
    b_reset ();
    b_str ("! ");
    b_str (name);
    b_ch ('=');
    b_num (v);
    b_str (" lim");
    b_num (lim);
    sink->line (buf);
    dbg_stack ();
    (void)sys;
}

/* The breadcrumb, joined.  This is the one that answers "what did it think
** it was doing" when a machine has stopped and the screen is frozen. */
void dbg_stack (void)
{
    unsigned char i, n = depth <= DBG_MAXDEPTH ? depth : DBG_MAXDEPTH;

    b_reset ();
    b_ch ('@');
    if (!n) b_str ("(top)");
    for (i = 0; i < n; ++i) {
        if (i) b_ch ('>');
        b_str (crumb[i]);
    }
    sink->line (buf);
}

/* Called at the head of the controller's turn loop.  A missed dbg_leave
** would otherwise make the depth column drift upwards for the rest of the
** run, and a depth that only ever grows tells you nothing. */
void dbg_reset_depth (void)
{
    depth = 0;
}

unsigned char dbg_key (unsigned char k)
{
    switch (k) {
    case DBG_K_TOGGLE:
        overlay ^= 1;
        if (overlay) screen_paint (); else screen_open ();
        return 1;
    case DBG_K_FILTER:
        filter = (unsigned char)(filter >= DBG_SYS_COUNT ? 0 : filter + 1);
        b_reset ();
        b_str ("filter ");
        b_str (filter >= DBG_SYS_COUNT ? (const char*)"all"
                                       : dbg_tag[filter]);
        sink->line (buf);
        return 1;
    case DBG_K_STACK:
        dbg_stack ();
        return 1;
    case DBG_K_STEP:
        stepping ^= 1;
        return 1;
    default:
        return 0;
    }
}

#endif  /* DEBUG */
