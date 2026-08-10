/* ------------------------------------------------------------------------
 * dbg.h - the trace facility, and what it costs when it is not wanted.
 *
 * There are two builds of this program:
 *
 *     make            build/urfinkel.prg       production, fast
 *     make debug      build/urfinkel-dbg.prg   traced
 *
 * and the difference between them is one -DDEBUG.  Without it every macro
 * below expands to `(void)0`, dbg.c compiles to an empty translation unit,
 * and there is no counter, no buffer and no branch left in the program.
 * That is the whole reason the facility is macros over functions rather
 * than a runtime `if (debugging)`: a 1 MHz machine cannot afford a test it
 * does not need, and a renderer that is 20% slower in production because
 * of instrumentation is not instrumented, it is broken.
 *
 * WHAT A TRACE LINE LOOKS LIKE
 *
 *     3mus  bank p=2 s=1
 *     |  |  |
 *     |  |  the message: short, one line, no wrapping
 *     |  the subsystem, three letters, so the eye can filter by column
 *     the call depth, so nesting is visible without indentation eating
 *     the 24 columns there are
 *
 * THE BREADCRUMB
 *
 * dbg_enter/dbg_leave keep a shadow return stack - not the 6502's, which
 * says nothing about intent, but a stack of (subsystem, name) pairs the
 * program pushes on the way in.  DBG_STACK() prints it joined:
 *
 *     gam>play>anim>glide
 *
 * which is the question you actually want answered when a machine stops:
 * not what address it stopped at, but what it thought it was doing.
 *
 * THE SINK IS INJECTED
 *
 * dbg.c formats lines; it does not decide where they go.  main() hands it
 * a `struct dbg_sink` at startup - the screen sink for a person watching,
 * the null sink for a headless run that only wants the breadcrumb kept.
 * Nothing in the trace path knows about the screen, so tracing the
 * renderer does not mean the renderer is inside its own trace.
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_DBG_H
#define URFINKEL_DBG_H

/* The subsystems.  Three letters each, deliberately: they are a column in
** a 24 character line, not a description.  Adding one means adding its
** name to dbg_tag[] in dbg.c and nothing else. */
#define DBG_GAM         0               /* the controller               */
#define DBG_BRD         1               /* the renderer                 */
#define DBG_RUL         2               /* the ruleset                  */
#define DBG_DIC         3               /* the lots                     */
#define DBG_BOT         4               /* urbot                        */
#define DBG_MUS         5               /* the sequencer                */
#define DBG_FRT         6               /* the cabinet                  */
#define DBG_TXT         7               /* words                        */
#define DBG_SYS_COUNT   8

#ifdef DEBUG

/* Where finished lines go.  A sink is a struct of function pointers so
** that it can be swapped at startup without the trace path knowing. */
struct dbg_sink {
    void (*line) (const char* s);       /* one finished line            */
    void (*open) (void);                /* claim whatever it draws on   */
};

extern const struct dbg_sink dbg_screen_sink;   /* scrolls on the screen */
extern const struct dbg_sink dbg_null_sink;     /* keeps the breadcrumb  */

void dbg_init  (const struct dbg_sink* sink);
void dbg_enter (unsigned char sys, const char* name);
void dbg_leave (void);
void dbg_say   (unsigned char sys, const char* msg);
void dbg_val   (unsigned char sys, const char* name, unsigned int v);
void dbg_val2  (unsigned char sys, const char* name,
                unsigned int a, unsigned int b);
void dbg_stack (void);
void dbg_reset_depth (void);            /* resync at the top of a loop  */
unsigned char dbg_key (unsigned char k);        /* 1 if the key was ours */

/* An index that has gone out of range, reported rather than acted on.
** On a machine with no memory protection an out-of-bounds write does not
** fault, it silently changes something else - and the something else is
** whatever the linker happened to put next, which is why the same bug
** shows up in a different place every time the program is rebuilt.  This
** is the only way to see one before it has already done its damage.  It
** prints the offending value AND the breadcrumb, then latches so a flood
** of the same failure does not scroll the evidence away. */
void dbg_bound (unsigned char sys, const char* name,
                unsigned int v, unsigned int lim);

#  define DBG_ENTER(s,n)    dbg_enter ((s), (n))
#  define DBG_LEAVE()       dbg_leave ()
#  define DBG_SAY(s,m)      dbg_say ((s), (m))
#  define DBG_VAL(s,n,v)    dbg_val ((s), (n), (unsigned int)(v))
#  define DBG_VAL2(s,n,a,b) dbg_val2 ((s), (n), (unsigned int)(a), \
                                      (unsigned int)(b))
#  define DBG_STACK()       dbg_stack ()
#  define DBG_DEPTH0()      dbg_reset_depth ()
#  define DBG_KEY(k)        dbg_key (k)
#  define DBG_BOUND(s,n,v,l) dbg_bound ((s), (n), (unsigned int)(v), \
                                        (unsigned int)(l))

#else

#  define DBG_ENTER(s,n)    ((void)0)
#  define DBG_LEAVE()       ((void)0)
#  define DBG_SAY(s,m)      ((void)0)
#  define DBG_VAL(s,n,v)    ((void)0)
#  define DBG_VAL2(s,n,a,b) ((void)0)
#  define DBG_STACK()       ((void)0)
#  define DBG_DEPTH0()      ((void)0)
#  define DBG_KEY(k)        (0)
#  define DBG_BOUND(s,n,v,l) ((void)0)

#endif

/* The keys, documented here so they are documented once.  They are the
** function keys because every other printable key on this machine already
** means something to the game.
**
**     f1   the overlay on or off
**     f2   cycle the subsystem filter: all -> gam -> brd -> ... -> all
**     f3   print the breadcrumb now
**     f4   step mode: every trace line waits for a key
*/
#define DBG_K_TOGGLE    133             /* f1 */
#define DBG_K_FILTER    137             /* f2 */
#define DBG_K_STACK     134             /* f3 */
#define DBG_K_STEP      138             /* f4 */

#endif
