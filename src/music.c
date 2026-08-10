/* ------------------------------------------------------------------------
 * music.c - the sequencer that decides what the interrupt should say.
 *
 * See music.h for the design and irq.s for the part that runs between the
 * raster lines.  The division is strict: this file never writes voice 1 or
 * the volume register, and the interrupt never makes a decision.
 *
 * Three clocks, coarsest last:
 *
 *   tick    1/200 s   the raster interrupt: one step of each arpeggio
 *   frame   1/50 s    this file: the grid, the level
 *   beat    40 frames the musical grid - bars, phrases, sections
 *
 * A SUBJECT OVER A HELD LINE, AND ONE UNCHANGING LEVEL.  TED has two tone
 * voices and a single global four-bit volume shared between them.
 *
 *   voice 1   the SUBJECT: the primary bank's figure, in real note values
 *             - halves among quarters, per step - repeating so that it is
 *             recognisable when it comes round.
 *   voice 2   the ARIA's held line: the secondary bank's two degrees an
 *             octave below, each sustained over a second, so it reads as
 *             harmony the subject is sung over rather than a second part
 *             competing with it.
 *
 * Both are built from the same chord and every step of each is a chord
 * DEGREE, so they cannot disagree whatever pairing of banks comes up.  On
 * the downbeat both restart from step 0 together.
 *
 * The volume register does not move at all.  See the note over VOL_BED for
 * why every attempt to use it as an instrument had to be taken back out:
 * there is one level for both voices, so shaping either one shapes the
 * other, and the result is a texture that pumps as a whole.  Expression
 * lives in pitch here, not in amplitude.
 * --------------------------------------------------------------------- */

#include "plus4.h"
#include "music.h"
#include "dice.h"                       /* rnd_below: the opening banks   */
/* The traced build carries fewer beds: -DDEBUG costs more than the machine
** has spare, and the shipped game keeps its music rather than paying for a
** bench tool.  Same engine, same flags, a shorter rotation.  See
** tools/bedtrim.py. */
#ifdef DEBUG
#include "song_dbg.h"
#else
#include "song.h"
#endif
#include "dbg.h"

/* --- the interrupt's tables (irq.s) ----------------------------------- */

extern unsigned char arp_lo[16], arp_hi[16], arp_len, arp_rate, arp_gate[16];
extern unsigned char arp2_lo[16], arp2_hi[16], arp2_len, arp2_rate, arp2_gate[16];
extern unsigned char arp_sync;
extern unsigned char snd_base_vol, snd_enable;

/* VOLATILE, and it is not decoration.  The interrupt bumps this once a
** frame; the C side reads it to work out how many frames it owes.  Without
** volatile, -Osir is entitled to read it ONCE and keep the value - and then
** `due` is zero for ever, the sequencer never advances a frame, and the
** music simply stops while everything else carries on.  It is the same trap
** plus4.h records for the TED registers, except the thing writing behind the
** compiler's back is our own interrupt rather than the hardware. */
extern volatile unsigned char music_frames;
extern unsigned char sfx_lo, sfx_hi, sfx_ticks;
void irq_install (void);
void irq_remove (void);

/* --- levels ----------------------------------------------------------- */
/*
** THE VOLUME REGISTER IS NEVER USED AS AN INSTRUMENT.
**
** TED's volume is four bits, it is GLOBAL, and it is shared by both
** voices - so anything written to it is written to everything sounding.
** An earlier engine here treated that as an opportunity: it swelled the
** level on an attack, ducked it rhythmically as a hurdy-gurdy buzz, and
** let effects seize it outright.  All of that is gone, because all of it
** has the same defect - the whole texture pumps, including the parts that
** were not meant to move, and on a machine with one level there is no way
** to shape one voice without shaping the other.
**
** The rule now is: THE REGISTER DOES NOT MOVE.  It is written once with
** VOL_BED and it stays there for as long as the machine is making a sound.
**
** A first attempt kept a decay - a note started at VOL_BED and fell one
** step before the next - on the grounds that "only reduce" still allowed
** downward shaping.  It does, and it was still wrong: there is one
** register, so a note's decay is also the other voice's decay, and what
** you hear is both voices breathing together on every note.  There is no
** amount of level movement on this chip that affects one voice only.
**
** So the expression lives entirely in PITCH now - two voices arpeggiating
** the same chord at different rates - and the level is furniture. */

#define VOL_SILENT      0               /* muted: nothing at all          */
#define VOL_BED         2               /* and there is no other level    */

/* --- the musical grid -------------------------------------------------
** A phrase is the progression once through; the bank walk and the key
** change are counted in phrases, in on_bar.  The rest of the grid is in
** the "musical time" section below. */

/* The chord per bar, the two instruments' banks, how many steps each figure
** has, and how many phrases each instrument holds a bank for (PRIM_EVERY,
** SEC_EVERY) all arrive from tools/songs.mml by way of song.h - see the
** "bed" section there.  What is left here is the grid the rates are
** counted in: the beat, the bar and the phrase. */

/* Chord tones, rebuilt each bar: root, third, fifth, octave, ninth. */
static unsigned char chord_tone[5];

/* --- state ------------------------------------------------------------ */

static unsigned char mode;
static unsigned char muted;
static unsigned char sfx_on = 1;        /* the `s` key; off for a demo    */
static unsigned char last_frame;

/* the grid */
static unsigned char bar;               /* bar within the phrase          */
static unsigned char phrase;            /* phrases since the bed started  */
static unsigned char prim_bank;
static unsigned char sec_bank;
static unsigned char prim_left;         /* phrases this bank still holds  */
static unsigned char sec_left;
static unsigned char bed_started;       /* has the bed ever played?       */

/* the generated bed */
static unsigned char amb_key;           /* the tonic, moved by fifths     */
static unsigned char amb_root;          /* this bar's degree above it     */
static unsigned char amb_minor;

/* What the player chose, and where a written bed had got to when a
** one-shot interrupted it. */
static unsigned char bed_choice = BED_FIRST_SONG;
static const unsigned char* bed_v1_p;
static const unsigned char* bed_v2_p;
static unsigned char bed_v1_left, bed_v2_left, bed_saved;
static unsigned char song_is_bed;       /* is the running song the bed?   */

/* Defined below with the rest of the bed selection; song_frame needs it. */
static void bed_advance (void);

/* Is the player asking for silence?  Consulted by everything that makes a
** sound, so that "off" cannot be overridden by a turn cheer or a capture.
** Declared up here rather than beside music_off because the things that
** have to obey it - music_song, sfx - come first in the file. */
static unsigned char silent (void)
{
    return (unsigned char)(bed_choice == BED_OFF);
}

/* the written song.
**
** Each voice is a STREAM of one-byte dictionary indices plus the
** DICTIONARY those index into - see the header of song.h.  The dictionary
** pointer is per-voice and per-song, so it is state exactly like the
** stream pointer is, and it has to be saved and restored with it when a
** fanfare borrows the engine from a bed. */
static const unsigned char* v1_p;
static const unsigned char* v2_p;
static const unsigned char* v1_start;
static const unsigned char* v2_start;
static const unsigned char* v1_dict;
static const unsigned char* v2_dict;
static unsigned char v1_left, v2_left;
static unsigned char looping;
static unsigned char running;


/* --- the level -------------------------------------------------------- */

/* There is no envelope.  This is the whole of it: on, or muted. */
static void hold_level (void)
{
    snd_base_vol = muted ? VOL_SILENT : VOL_BED;
}

/* --- musical time -----------------------------------------------------
**
** A BPM THAT THE TWO CLOCKS AGREE ON.
**
** There are two clocks and they must not drift: the sequencer runs on
** frames (50 Hz, PAL) and the arpeggio runs on interrupt ticks (200 Hz).
** A tempo is only usable here if a beat is a whole number of BOTH.
**
**     beat = 40 frames = 160 ticks   ->  50*60/40 = 75 bpm exactly
**     16th = 10 frames =  40 ticks
**     bar  = 160 frames = 640 ticks  =  3.2 s
**
** 75 bpm is chosen for that reason and not for taste: every subdivision
** down to a sixteenth lands on an exact frame AND an exact tick, so the
** figure and the harmony can never slide against one another however long
** the machine runs.  Changing the tempo means finding another divisor of
** both 50 and 200 per beat - 60, 75, 100 and 150 bpm work; 90 and 110 do
** not, and would drift. */

#define BPM                 75
#define FRAMES_PER_BEAT     40          /* 50 Hz / (75/60)                */
#define FRAMES_PER_16TH     10
#define TICKS_PER_16TH      40          /* 200 Hz                         */
#define SIXTEENTHS_PER_BAR  16
#define BEATS_PER_BAR       4
#define FIGURE_STEPS        16          /* a figure is one bar long       */

/* WHERE BOTH VOICES STOP.
**
** Rhythm is not only where notes start, it is where they end, and on two
** voices sharing one texture it matters most that they end TOGETHER.  Both
** gate tables carry a rest at the same two steps - the last sixteenth of
** each half-bar - so twice a bar everything stops at once and the bar has
** a shape instead of being a continuous sound with a moving pitch.
**
** These are indices into the sixteen, so REST_MID closes the first half
** and REST_END closes the bar. */
#define REST_MID            7
#define REST_END            15
#define STEP_SOUNDS(i)      ((unsigned char)((i) != REST_MID && (i) != REST_END))

/* Where each voice sits.  The bass is the LOWEST note of the chord and it
** is what lands on the downbeat; the figure sings an octave and a half
** above it. */
#define BASS_MIDI           50          /* D3 - the floor TED can sound   */
#define ARP_MIDI            62          /* D4                             */

#define NOTE_IDX(m)         ((unsigned char)((m) - NOTE_BASE_MIDI))

/* --- the generated bed ------------------------------------------------ */

/* The scale the passing notes come from.  A figure that alternated chord
** tones with arbitrary semitones would be chromatic noise; alternating
** them with SCALE tones is what makes the in-between notes sound like
** ornament rather than error. */
static const unsigned char scale_min[7] = { 0, 2, 3, 5, 7, 9, 10 };  /* dorian */
static const unsigned char scale_maj[7] = { 0, 2, 4, 5, 7, 9, 11 };  /* ionian */

static unsigned char lead_voice;        /* which voice has the figure     */
static unsigned char frame_in_16;
static unsigned char sixteenth;         /* 0..15 within the bar           */

static void chord_build (void)
{
    chord_tone[0] = 0;                  /* the root - the bass note       */
    chord_tone[1] = amb_minor ? 3 : 4;
    chord_tone[2] = 7;
    chord_tone[3] = 12;
    chord_tone[4] = 14;
}

/* The next scale tone above a chord tone, keeping its octave. */
static unsigned char step_above (unsigned char c)
{
    const unsigned char* sc = amb_minor ? scale_min : scale_maj;
    unsigned char oct    = (unsigned char)((c / 12) * 12);
    unsigned char within = (unsigned char)(c % 12);
    unsigned char i;

    for (i = 0; i < 7; ++i)
        if (sc[i] > within) return (unsigned char)(oct + sc[i]);
    return (unsigned char)(oct + 12);
}

static void set_step (unsigned char* lo, unsigned char* hi,
                      unsigned char at, unsigned char midi)
{
    unsigned char idx = NOTE_IDX (midi);

    if (idx >= NOTE_COUNT) idx = NOTE_COUNT - 1;
    lo[at] = note_lo[idx];
    hi[at] = note_hi[idx];
}

/* THE FIGURE: sixteen notes across one bar, and it is a PENDULUM rather
** than a staircase.
**
** The first version walked upward - chord tone, the scale tone above it,
** the next chord tone, the scale tone above THAT - so every step went up
** and the figure climbed out of its own register, arriving back at the
** bottom only because the bar ended.  That is a staircase, and a staircase
** has no home in it.
**
** Every other note is now the BASE NOTE.  The figure leaves it, returns to
** it, leaves it for somewhere else, returns again:
**
**     root  a   root  b   root  c   root  d   ...
**
** which keeps the root sounding through the whole bar - reinforcing the
** bass on every second sixteenth - and makes the excursions read as
** ornament hung off a fixed point instead of a scale being climbed.
**
** The excursions alternate between a chord tone and the scale tone above
** it, so half of them are consonances and half are the passing notes
** between them.  The bank supplies WHICH tones and in what order, which is
** what makes each bank a different elaboration of the same progression
** rather than a different piece of music.
**
** Step 14 is the last that sounds (15 rests), and it is a root - so the
** bar closes on the base note and the next downbeat restates it. */
static void build_figure (unsigned char* lo, unsigned char* hi,
                          unsigned char* gate,
                          unsigned char base, const struct bank* pb)
{
    unsigned char i, s = 0, deg, c;

    for (i = 0; i < FIGURE_STEPS; ++i) {
        if ((i & 1) == 0) {
            c = chord_tone[0];                  /* home, every other note */
        } else {
            deg = chord_tone[pb->step[s % pb->len]];
            c   = (unsigned char)((s & 1) ? step_above (deg) : deg);
            ++s;
        }
        set_step (lo, hi, i, (unsigned char)(base + c));
        gate[i] = STEP_SOUNDS (i);
    }
}

/* THE RHYTHM SECTION, sixteen steps.
**
** This voice used to HOLD - the root for the first half-bar, the fifth for
** the second, two half notes under sixteen sixteenths.  That is an organ
** tone, and an organ tone has no pulse in it: with both voices sounding
** continuously the bar had harmony and no metre, which is what "it needs
** rhythm" means.
**
** It now strikes instead of holding, on 1, 3 and the AND of 3:
**
**     step   0 . . . | 4 . . . | 8 . 10. | 12. . .
**     hit    X       |         | X   X   |
**     beat   1       | 2       | 3   3&  | 4
**
** Three hits and thirteen rests. The rests are the point - a note that
** stops is what makes the next one an attack - and on this chip a rest is
** free, because the gate table clears the voice's enable bit for that tick
** rather than touching the one global level. Striking the bass and letting
** it fall silent is the nearest thing TED has to a drum, and it costs
** nothing the drone was not already costing.
**
** WHY 1, 3, 3& AND NOT 2 AND 4. The backbeat wants a snare, which wants
** noise, which on TED is voice 2's ALTERNATIVE - it cannot be a pitch and
** a noise at once, and this voice is carrying the whole bass line. So the
** pattern is the one a kick drum plays under a rock bar rather than the
** one a snare plays over it: the downbeat, the half-bar, and the push off
** the half-bar that throws the ear into beat 4. TED noise percussion is
** still open as backlog 11.8.
**
** The pitches follow the harmony, not the pattern: every hit is the bar's
** own bass, so the rhythm walks the progression rather than sitting on a
** pedal. The bank still gets a say in the third hit - the push - which is
** what keeps four banks sounding like four elaborations of one bass line
** instead of one bass line played four times. */

#define HIT_1               0           /* the downbeat                   */
#define HIT_3               8           /* the half-bar                   */
#define HIT_3AND            10          /* the push off it                */

static void hit_pattern (unsigned char* lo, unsigned char* hi,
                         unsigned char* gate,
                         unsigned char root, unsigned char answer)
{
    unsigned char i;

    for (i = 0; i < FIGURE_STEPS; ++i) {
        /* Every step still carries a pitch even though most of them are
        ** silent: the gate decides what sounds, and leaving a stale
        ** frequency behind a closed gate is how a voice comes back on the
        ** wrong note when the next bar opens it. */
        set_step (lo, hi, i, (unsigned char)(i == HIT_3AND ? answer : root));
        gate[i] = (unsigned char)(i == HIT_1 || i == HIT_3 || i == HIT_3AND);
    }
}

/* THE SPARKLE IS GONE, and the reason is worth keeping.
**
** It fired a chord tone from SPARK_MIDI - D5, plus the key, plus the root,
** plus up to a fifth on top - which lands near the ceiling of what TED
** will sound and well above everything else in the texture.  Worse, it
** fired it through `sfx`, and an effect PRE-EMPTS VOICE 1: every sparkle
** punched a hole in the melody to play a high note over it.
**
** Fibonacci spacing made it arrhythmic on purpose, which is the right idea
** for punctuation and the wrong one entirely underneath a march.  What is
** left is the chord progression and a rhythm section, which is what was
** asked for.  Its echoes went with it: an echo of nothing is nothing.
*/

/* THE DOWNBEAT.  Everything harmonic happens here and nowhere else.
**
** The bass lands on the root - the lowest note of the chord - which is
** what makes a bar audible as a bar, and the figure restarts from step 0
** in the same interrupt tick, so the two voices state the chord together.
**
** THE DUEL.  The figure changes hands every two bars: one voice runs the
** sixteenths while the other holds the organ tone beneath it, and then
** they swap.  The subject is the same either way - it is the same bank
** elaborating the same progression - so what the ear follows is one line
** being passed between two hands, which is the fugal gesture this is
** after. */
/* Load this bar's chord and both voices' parts, without advancing
** anything.  Split out from on_bar because resuming the bed after a song
** has to restate the current bar rather than step past it. */
static void bar_load (void)
{
    const struct bank* pb = &primary[prim_bank];
    unsigned char bass, fifth, fig;

    amb_root = progression[bar];
    chord_build ();

    lead_voice = (unsigned char)((bar >> 1) & 1);

    /* The downbeat is the ROOT - the lowest note of the chord - always,
    ** whatever the bank says.  That is what makes a bar audible as a bar,
    ** and it is not something a bank is allowed to elaborate away.
    **
    ** The bank gets the SECOND half note instead: the secondary bank's two
    ** degrees still choose, but only the one that does not land on the
    ** downbeat.  So bank a holds the root right through, b answers it with
    ** the fifth, c and d with the fifth and the root - four elaborations of
    ** the same bass line rather than four different bass lines. */
    bass  = (unsigned char)(BASS_MIDI + amb_key + amb_root);
    fifth = (unsigned char)(bass + chord_tone[secondary[sec_bank][1]]);
    fig   = (unsigned char)(ARP_MIDI + amb_key + amb_root + pb->oct);

    /* Length is zeroed first and set last, so the interrupt never reads a
    ** table that is halfway between two chords. */
    arp_len = arp2_len = 0;
    if (lead_voice == 0) {
        build_figure (arp_lo, arp_hi, arp_gate, fig, pb);
        hit_pattern (arp2_lo, arp2_hi, arp2_gate, bass, fifth);
    } else {
        build_figure (arp2_lo, arp2_hi, arp2_gate,
                      (unsigned char)(fig - 12), pb);
        hit_pattern (arp_lo, arp_hi, arp_gate,
                     (unsigned char)(bass + 12), (unsigned char)(fifth + 12));
    }
    /* Both voices step at the same rate over the same sixteen positions,
    ** so their rests cannot drift apart. */
    arp_rate = arp2_rate = TICKS_PER_16TH;
    arp_len  = arp2_len  = FIGURE_STEPS;
    arp_sync = 1;

    DBG_VAL2 (DBG_MUS, "bar", bar, lead_voice);
}

static void on_bar (void)
{
    bar_load ();

    /* --- the long structure, advanced once a bar --------------------- */
    if (++bar >= PROGRESSION_BARS) {
        bar = 0;
        ++phrase;

        /* Each instrument walks its banks, holding each for the number of
        ** phrases notated in songs.mml.  Counted down rather than derived
        ** from `phrase`, so the walk survives the counter wrapping. */
        if (--prim_left == 0) {
            prim_left = PRIM_EVERY;
            prim_bank = (unsigned char)((prim_bank + 1) % BANKS);
        }
        if (--sec_left == 0) {
            sec_left = SEC_EVERY;
            sec_bank = (unsigned char)((sec_bank + 1) % BANKS);
        }

        /* The circle: the whole progression moves up a fifth each phrase,
        ** so the piece walks all twelve keys rather than restating one. */
        amb_key = (unsigned char)((amb_key + 7) % 12);

        /* And every second phrase the mode flips, which is the largest
        ** gesture the bed makes. */
        if ((phrase & 1) == 0) amb_minor ^= 1;
    }
}

static void ambient_frame (void)
{
    if (++frame_in_16 >= FRAMES_PER_16TH) {
        frame_in_16 = 0;
        if (sixteenth == 0)                    on_bar ();
        if (++sixteenth >= SIXTEENTHS_PER_BAR) sixteenth = 0;
    }

    hold_level ();
}

/* --- the written song ------------------------------------------------- */

static void song_frame (void)
{
    unsigned char b, n;

    if (v1_left) --v1_left;
    if (v1_left == 0) {
        if (*v1_p == SONG_END) {
            if (!looping) {
                running = 0;
                /* A written bed reaching its end hands over to the next
                ** one, so the music moves along by itself rather than
                ** looping one piece until somebody presses a key. */
                if (song_is_bed) { bed_advance (); }
                else         { music_resume (); }
                return;
            }
            v1_p = v1_start;
        }
        b = *v1_p++;
        if (b == SONG_LIT) {            /* spelled out, not in the table */
            n       = *v1_p++;
            v1_left = *v1_p++;
        } else {
            /* The index is doubled to reach a two-byte entry, and the
            ** double is done in 16 bits: a dictionary may hold 254 entries
            ** and 2*253 does not fit the char the index arrived in. */
            const unsigned char* e = v1_dict + ((unsigned)b << 1);
            n       = e[0];
            v1_left = e[1];
        }
        if (n == SONG_REST) {
            snd_enable &= (unsigned char)~0x10;
        } else {
            snd_enable |= 0x10;
            arp_len   = 0;
            arp_lo[0] = note_lo[n];
            arp_hi[0] = note_hi[n];
            arp_len   = 1;              /* one "chord tone": the melody   */
        }
    }

    if (v2_left) --v2_left;
    if (v2_left == 0) {
        if (*v2_p == SONG_END) v2_p = v2_start;
        b = *v2_p++;
        if (b == SONG_LIT) {
            n       = *v2_p++;
            v2_left = *v2_p++;
        } else {
            const unsigned char* e = v2_dict + ((unsigned)b << 1);
            n       = e[0];
            v2_left = e[1];
        }
        if (n == SONG_REST) {
            snd_enable &= (unsigned char)~0x20;
        } else {
            snd_enable |= 0x20;
            TED_S2FREQ_LO = note_lo[n];
            TED_S2FREQ_HI = note_hi[n];
        }
    }

    hold_level ();
}

/* --- the public face -------------------------------------------------- */

void music_init (void)
{
    arp_len  = 0;
    sfx_ticks = 0;
    snd_enable = 0x30;
    snd_base_vol = VOL_SILENT;
    mode = MUS_OFF;
    muted = 0;
    irq_install ();
}

void music_shutdown (void)
{
    irq_remove ();
}

/* THE BED RESUMES; IT DOES NOT RESTART.
**
** This function is called every time a written song finishes - a turn
** cheer, the fanfare, the victory theme - and it used to reset the entire
** grid to zero: phrase 0, bank A, the tonic, bar 0.  A turn cheer happens
** every time the players change, which is to say constantly, so the bed
** was being sent back to its opening bar every few seconds and the whole
** structure above the bar - the bank walk, the key rising a fifth a
** phrase, the mode flip every second phrase - could never be heard.  It
** was composed and then never allowed to happen.
**
** So the grid is now left exactly as the song found it and the bed picks
** up mid-phrase, on the bank and in the key it was in.  Only a bed that
** has never played gets initialised, and that one opens on a RANDOMLY
** CHOSEN pair of banks, so two sittings do not begin with the same figure.
**
** Effects (sfx) never needed any of this: they borrow voice 1 for a few
** ticks and the arpeggio simply resumes underneath when the count runs
** out.  It was only ever songs that reset the world. */
void music_ambient (void)
{
    DBG_SAY (DBG_MUS, "bed");
#ifdef NO_MUSIC
    mode = MUS_OFF; snd_base_vol = VOL_SILENT; arp_len = 0; return;
#else
    mode      = MUS_AMBIENT;
    running   = 0;
    snd_enable = 0x30;

    if (!bed_started) {
        bed_started = 1;
        prim_bank = rnd_below (BANKS);
        sec_bank  = rnd_below (BANKS);
        prim_left = PRIM_EVERY;
        sec_left  = SEC_EVERY;
        frame_in_16 = 0;
        sixteenth   = 0;
        lead_voice  = 0;
        bar       = 0;
        phrase    = 0;
        amb_key   = 0;
        amb_minor = 1;                  /* dorian: a minor third          */
        DBG_VAL2 (DBG_MUS, "open", prim_bank, sec_bank);
    }

    /* Whichever bar the grid is on - bar 0 on a fresh bed, wherever the
    ** song interrupted on a resumed one. */
    amb_root = progression[bar];
    bar_load ();
    hold_level ();
    last_frame = music_frames;
#endif
}

void music_song (unsigned char which, unsigned char loop)
{
#ifdef NO_MUSIC
    (void)which; (void)loop; return;
#else
    DBG_VAL (DBG_MUS, "song", which);
    if (which >= SONG_COUNT) return;
    /* A fanfare or a turn cheer must not talk over a player who has turned
    ** the music off - and music_song is what would otherwise re-enable
    ** both voices behind their back. */
    if (silent ()) { music_off (); return; }

    /* A one-shot arriving over a written bed - a turn cheer, the fanfare -
    ** takes note of where the bed was, so music_resume can put it back
    ** mid-phrase instead of restarting it.  A cheer happens every time the
    ** players change, so without this a written bed would never be heard
    ** past its opening bar. */
    if (!loop && mode == MUS_SONG && song_is_bed) {
        bed_v1_p    = v1_p;
        bed_v2_p    = v2_p;
        bed_v1_left = v1_left;
        bed_v2_left = v2_left;
        bed_saved   = 1;
    }

    /* Whatever starts now is a one-shot unless bed_song says otherwise,
    ** and it must be set AFTER the save above has read the old value. */
    song_is_bed = 0;

    v1_p = v1_start = song_v1[which];
    v2_p = v2_start = song_v2[which];
    v1_dict = song_k1[which];
    v2_dict = song_k2[which];
    v1_left = v2_left = 0;
    looping = loop;
    running = 1;
    mode = MUS_SONG;
    snd_enable = 0x30;
    /* A written song drives voice 2 note by note from song_frame, so the
    ** bed's second arpeggio must stand down or both would write $FF0F. */
    arp2_len = 0;
    hold_level ();
    last_frame = music_frames;
#endif
}

/* OFF MEANS SILENT, and it takes three things to be silent.
**
** The first attempt at this cleared the two figure lengths and left voice
** 1 ENABLED, on the reasoning that effects should still be audible with
** the music off.  That leaves a note RINGING: with no figure to advance,
** nothing ever writes voice 1 a new pitch, so the last frequency it was
** given simply sits in the register and sounds forever.  Clearing a
** figure stops the notes CHANGING; it does not stop the voice.
**
** So all three have to go: the figures, both enable bits, and the level.
** Any effect still counting down is cancelled too, because "off" that
** still clicks when the lots land is not what anybody means by off. */
void music_off (void)
{
    mode      = MUS_OFF;
    running   = 0;
    arp_len   = 0;
    arp2_len  = 0;
    sfx_ticks = 0;                      /* cancel anything mid-effect     */
    snd_enable   = 0x00;                /* both voices off, not just quiet */
    snd_base_vol = VOL_SILENT;
}



/* --- which bed ------------------------------------------------------- */

static void bed_song (unsigned char which)
{
    /* THE RESUME POINT IS READ BEFORE music_song RUNS, and that ordering is
    ** the whole of this function.
    **
    ** music_song saves the bed's position whenever a non-looping song
    ** starts over a running written bed - which is what puts a bed back
    ** after a turn cheer.  But a bed handing over to the NEXT bed goes
    ** through here too, and it arrives from inside song_frame with the old
    ** song sitting on its own SONG_END.  Reading bed_saved after the call
    ** therefore picked up a "resume point" that music_song had just written
    ** from the song that had finished, and seeked the incoming bed straight
    ** to it: the new bed ended on its next frame, handed over again, and
    ** the whole rotation ran past in a dozen frames.  Measured before the
    ** fix: the first handover at frame 818, the next five two frames apart.
    ** music_bed clearing bed_saved first did not help, because the clear
    ** happened before the write that undid it.
    **
    ** Taking the copy first means only a resume that was already pending
    ** when this was called can move the pointers - a cheer ending - and a
    ** deliberate change of bed, or one bed following another, starts the
    ** new one where it begins. */
    unsigned char        resume = bed_saved;
    const unsigned char* r1     = bed_v1_p;
    const unsigned char* r2     = bed_v2_p;
    unsigned char        l1     = bed_v1_left;
    unsigned char        l2     = bed_v2_left;

    /* Started NOT looping, because the end of one written bed is what
    ** starts the next.  song_frame tells the two cases apart by song_is_bed:
    ** a fanfare or a turn cheer ending puts the bed back, a bed ending moves
    ** along to the next one. */
    music_song (which, 0);
    song_is_bed = 1;
    bed_saved   = 0;
    if (resume) {
        v1_p    = r1;
        v2_p    = r2;
        v1_left = l1;
        v2_left = l2;
    }
}

void music_resume (void)
{
    /* The written beds are a table now rather than a switch: there are
    ** seventeen of them and the list is generated, so a case per bed would
    ** be a second copy of it to forget to update. */
    static const unsigned char bed_song_id[BED_SONG_COUNT] = {
#define X(id, label) id,
        BED_SONG_LIST
#undef X
    };

    if (bed_choice < BED_SONG_COUNT) bed_song (bed_song_id[bed_choice]);
    else if (bed_choice == BED_GEN)  music_ambient ();
    else                             music_off ();
}

void music_bed (unsigned char n)
{
    bed_choice = (unsigned char)(n % BED_COUNT);
    bed_saved  = 0;                     /* a deliberate change starts fresh */
    if (!silent ()) snd_enable = 0x30;
    music_resume ();
}

void music_bed_next (void)
{
    music_bed ((unsigned char)(bed_choice + 1));
}

/* A written bed has finished.  Move to the next one, wrapping back to the
** first WRITTEN bed rather than to the top of the cycle - the generated bed
** and silence have no end, so handing over to either would stop the
** rotation dead and look like the music had simply stopped. */
static void bed_advance (void)
{
    unsigned char n = (unsigned char)(bed_choice + 1);

    if (n > BED_LAST_SONG) n = BED_FIRST_SONG;
    DBG_VAL (DBG_MUS, "next", n);
    music_bed (n);
}

unsigned char music_bed_now (void)
{
    return bed_choice;
}

void music_mute (unsigned char m)
{
    muted = m;
    if (m) snd_base_vol = VOL_SILENT;
}

void music_sfx (unsigned char on)
{
    sfx_on = on;
    /* Anything already sounding stops with it, or turning effects off
    ** leaves the last click ringing until the arpeggio takes the voice
    ** back - which is exactly the noise the player was trying to stop. */
    if (!on) sfx_ticks = 0;
}

unsigned char music_sfx_on (void)
{
    return sfx_on;
}

unsigned char music_muted (void)
{
    return muted;
}

unsigned char music_busy (void)
{
    return (unsigned char)(mode == MUS_SONG && running);
}

/* The cooperative half of the sequencer.  It is called from every wait
** loop in the game, catches up on however many frames have passed since
** the last call, and never blocks - so gameplay code never waits on the
** music and the music advances whenever the game breathes. */
void music_service (void)
{
    unsigned char now = music_frames;
    unsigned char due = (unsigned char)(now - last_frame);

    if (!due) return;
    last_frame = now;
    if (due > 8) due = 8;               /* after a long repaint, catch up
                                        ** but do not stall doing it     */

    while (due--) {
        if (mode == MUS_AMBIENT) ambient_frame ();
        else if (mode == MUS_SONG) song_frame ();
    }
}

/* --- effects ---------------------------------------------------------- */

/* Effect pitches as TED register values, and how many 200 Hz ticks they
** last.  They take voice 1 - the arpeggio's voice - and they take the
** volume register with them, which is the only way to give anything
** priority on a chip with one global level.  The bed plays at 2 or 3 and
** an effect jumps the register to 6, so it is unmistakably on top. */
static const unsigned int sfx_note[8] = {
    880, 820, 900, 130, 760, 900, 60, 700
};
static const unsigned char sfx_len[8] = {
    6,   10,  14,  40,  24,  30,  20, 4
};

void sfx (unsigned char kind)
{
    unsigned int n;

#ifdef NO_MUSIC
    (void)kind; return;
#else
    if (kind > 7 || muted || silent () || !sfx_on) return;
    n = sfx_note[kind];
    sfx_lo    = (unsigned char)(n & 0xFF);
    sfx_hi    = (unsigned char)((n >> 8) & 0x03);
    sfx_ticks = sfx_len[kind];
#endif
}
