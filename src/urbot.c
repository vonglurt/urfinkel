/* ------------------------------------------------------------------------
 * urbot.c - four doctrines over one scan.
 *
 * A port of the frozen edition's 4500-4684.  The structure is deliberately
 * unchanged: one census, one scan into five registers, then a priority
 * order over those registers per doctrine.  Only the narration is cheaper
 * - a chronicle line cost 404 ms in BASIC, which is why URBOT's five-line
 * deliberation used to take two seconds of every turn it took.
 * --------------------------------------------------------------------- */

#include "rules.h"
#include "text.h"
#include "dice.h"
#include "urbot.h"

unsigned char doctrine[2];
unsigned char doctrine_switched[2];

static const char* const doctrine_name[DOCTRINES] = {
    "one at a time",
    "fill the board",
    "run for it",
    "hunt and hold"
};

void urbot_new_match (void)
{
    doctrine[0] = rnd_below (DOCTRINES);
    doctrine[1] = rnd_below (DOCTRINES);
    doctrine_switched[0] = 0;
    doctrine_switched[1] = 0;
}

/* The five opportunity registers, filled by one pass over the legal moves. */
static unsigned char op_bear;           /* a piece that can bear off      */
static unsigned char op_capture;        /* a piece that can take a foe    */
static unsigned char op_rosette;        /* a piece that can seize one     */
static unsigned char op_enter;          /* the cheapest new piece         */
static unsigned char op_lead;           /* the furthest-advanced runner   */

static void scan (unsigned char player)
{
    unsigned char* mine   = piece[player];
    unsigned char* theirs = piece[player ^ 1];
    unsigned char  j, k, d;
    signed char    best = -1;

    op_bear = op_capture = op_rosette = op_enter = op_lead = 0;

    for (j = 1; j <= PIECES; ++j) {
        d = legal[j];
        if (!d) continue;

        if (d == SQ_HOME) op_bear = j;
        else if (rosette[d]) op_rosette = j;

        if (d >= SHARED_FIRST && d <= SHARED_LAST)
            for (k = 1; k <= PIECES; ++k)
                if (theirs[k] == d) op_capture = j;

        if (mine[j] == SQ_POOL && !op_enter) op_enter = j;
        if (mine[j] > SQ_POOL && (signed char)mine[j] > best) {
            best = (signed char)mine[j];
            op_lead = j;
        }
    }
}

/* Each doctrine is a priority order over the five registers, and the
** sentence it speaks is the branch that fired. */
static unsigned char resolve (unsigned char doc, const char** why)
{
    switch (doc) {

    case DOC_ONE_AT_A_TIME:
        if (op_bear)    { *why = "the count is exact - my runner leaves"; return op_bear; }
        if (op_capture && op_capture == op_lead)
                        { *why = "my runner can strike on the way";       return op_capture; }
        if (op_lead)    { *why = "one at a time - i drive my lead piece"; return op_lead; }
        if (op_rosette) { *why = "none afield - i open on a rosette";     return op_rosette; }
        if (op_enter)   { *why = "blocked - the rules force a new piece"; return op_enter; }
        break;

    case DOC_FILL_BOARD:
        if (op_capture) { *why = "a crowd wants victims - i capture";     return op_capture; }
        if (op_enter)   { *why = "i flood the field - another enters";    return op_enter; }
        if (op_rosette) { *why = "a rosette holds ground and re-throws";  return op_rosette; }
        if (op_bear)    { *why = "no room to grow - i bear one off";      return op_bear; }
        if (op_lead)    { *why = "i shuffle the throng forward";          return op_lead; }
        break;

    case DOC_RUN_FOR_IT:
        if (op_bear)    { *why = "run for it - the exact count sends it"; return op_bear; }
        if (op_lead)    { *why = "run for it - my furthest piece runs";   return op_lead; }
        if (op_rosette) { *why = "a rosette on the way - free throw";     return op_rosette; }
        if (op_capture) { *why = "the road is guarded - i clear it";      return op_capture; }
        if (op_enter)   { *why = "no runner left - a new one starts";     return op_enter; }
        break;

    default: /* DOC_HUNT_AND_HOLD */
        if (op_capture) { *why = "blood in the corridor - i capture";     return op_capture; }
        if (op_rosette) { *why = "i seize the rosette and hold it";       return op_rosette; }
        if (op_bear)    { *why = "this hunt is done - it goes home";      return op_bear; }
        if (op_lead)    { *why = "i stalk forward and wait for prey";     return op_lead; }
        if (op_enter)   { *why = "i post a fresh sentry on the road";     return op_enter; }
        break;
    }
    return 0;
}

unsigned char urbot_choose (unsigned char player)
{
    unsigned char idle   = count_in_pool (player);
    unsigned char afield = count_afield (player);
    unsigned char home   = count_at_home (player);
    unsigned char pick, guard;
    const char*   why = "no clear path - i trust the gods";

    /* An endgame board wants different play from an opening one, so once
    ** three pieces are home the doctrine is re-drawn - latched, so it
    ** happens exactly once a match and a single game can show more than
    ** one intention. */
    if (home >= 3 && !doctrine_switched[player]) {
        doctrine_switched[player] = 1;
        doctrine[player] = rnd_below (DOCTRINES);
        say ("three are home - i change my mind");
    }

    ln_reset ();
    ln_str ("mind: ");
    ln_str (doctrine_name[doctrine[player]]);
    say_line ();

    ln_reset ();
    ln_num (idle);   ln_str (" idle, ");
    ln_num (afield); ln_str (" afield, ");
    ln_num (home);   ln_str (" home");
    say_line ();

    scan (player);
    pick = resolve (doctrine[player], &why);

    if (!pick) {
        /* The fallback, so URBOT can never stall.  Bounded because a
        ** doctrine with nothing to say only happens when at least one
        ** move is legal - the controller checked. */
        guard = 0;
        do {
            pick = (unsigned char)(1 + rnd_below (PIECES));
        } while (!legal[pick] && ++guard < 40);
        if (!legal[pick])
            for (pick = 1; pick <= PIECES && !legal[pick]; ++pick) ;
    }

    say (why);

    ln_reset ();
    ln_str ("so: piece ");
    ln_num (pick);
    ln_str (" goes to ");
    if (legal[pick] == SQ_HOME) ln_str ("home");
    else                        ln_num (legal[pick]);
    say_line ();

    return pick;
}
