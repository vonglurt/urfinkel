/* ------------------------------------------------------------------------
 * music.h - two voices, one volume, and what can be done with them.
 *
 * TED gives you exactly two tone channels and a SINGLE four-bit volume
 * shared between them.  There is no per-channel volume, so a quiet pad
 * underneath a loud melody is not physically available on this machine.
 * Everything here is built around that fact rather than against it:
 *
 *   - chords are ARPEGGIATED, fast enough that the ear fuses them - the
 *     classic 8-bit chord;
 *   - BOTH voices do it, from the same chord and at different rates, and
 *     both restart together on the downbeat;
 *   - the one global volume DOES NOT MOVE.  Modulating it was tried three
 *     ways - an attack envelope, a hurdy-gurdy buzz, effects seizing the
 *     level for priority - and every one of them moved both voices at
 *     once, because there is only one register.  Expression is in pitch.
 *
 * Two layers run at once and at different rates.  The interrupt (irq.s)
 * ticks 200 times a second and does the arpeggio and the buzz.  The C
 * sequencer here runs at 50 Hz off the frame counter the interrupt bumps,
 * and decides chords, songs and sparkles.  Nothing blocks: music_service
 * is called from every wait loop in the game.
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_MUSIC_H
#define URFINKEL_MUSIC_H

/* The song names, generated from tools/songs.mml.  Identifiers only - the
** note tables live in song.h, which only music.c may include, because it
** defines them rather than declaring them. */
#include "song_ids.h"

/* Effects.  They pre-empt voice 1 for a moment and then hand it back. */
#define SFX_CLICK       0               /* a lot striking the floor       */
#define SFX_SETTLE      1               /* a lot coming to rest           */
#define SFX_THROW       2               /* the count is read off          */
#define SFX_CAPTURE     3               /* a foe sent back to the pool    */
#define SFX_ROSETTE     4               /* another throw                  */
#define SFX_HOME        5               /* a piece bears off              */
#define SFX_ERROR       6               /* that key means nothing         */
#define SFX_STEP        7               /* one square of travel           */

/* What the engine is doing right now. */
#define MUS_OFF         0
#define MUS_AMBIENT     1               /* the generated bed              */
#define MUS_SONG        2               /* a compiled song from songs.mml */

/* --- what is playing under the game ----------------------------------
** Three beds and silence, cycled by `m`.  One of them is generated and
** two are written, which is why this is a separate idea from MUS_* above:
** that says HOW the engine is producing sound, this says WHAT the player
** asked for, and the two are not the same question. */
/* THE WRITTEN SONGS COME FIRST, and the order is not arbitrary.
**
** These are what `m` walks and what the bed rotation walks, and the two
** want opposite things from the list: the player cycling by hand wants to
** reach the real music immediately, and the rotation must never wander
** into something that has no end.  Putting the four written songs at the
** bottom of the range serves both - the rotation is simply "stay inside
** 0..BED_LAST_SONG", and the generated bed and silence sit past it where
** only a deliberate keypress reaches them.
**
** It also means the game OPENS on a written song, so the rotation is
** running from the first bar of the first match without anybody pressing
** anything. It used to open on the generated bed, which has no end, so
** the hand-over feature could not happen at all until the player found
** the `m` key. */
/* The written beds are 0..BED_SONG_COUNT-1 and they are GENERATED: the
** list comes from tools/midibed.py, which transcribes assets/midi and
** packs in as many as the ROM will hold.  There is no hand-written table
** of them here, because a hand-written one drifts the moment a source is
** added or the budget moves. */
#include "song_beds.h"

#define BED_GEN         (BED_SONG_COUNT)        /* generated bed - no end */
#define BED_OFF         (BED_SONG_COUNT + 1)    /* silence                */
#define BED_COUNT       (BED_SONG_COUNT + 2)

/* The written span, inclusive.  A song that ends hands over to the next
** one inside this range and wraps at the top of it; BED_GEN and BED_OFF
** have no end, so the rotation must not be able to fall into them. */
#define BED_FIRST_SONG  0
#define BED_LAST_SONG   (BED_SONG_COUNT - 1)

void music_bed (unsigned char n);
void music_bed_next (void);             /* 1 -> 2 -> ... -> off -> 1      */
unsigned char music_bed_now (void);

/* Put back whatever the player chose, after a fanfare or a turn cheer has
** finished borrowing the engine.  A written bed resumes at the note it was
** interrupted on rather than restarting. */
void music_resume (void);

void music_init (void);
void music_shutdown (void);
void music_service (void);              /* call from every wait loop      */
void music_ambient (void);
void music_song (unsigned char which, unsigned char loop);
void music_off (void);
void music_mute (unsigned char m);      /* the M key                      */
unsigned char music_muted (void);
unsigned char music_busy (void);        /* a non-looping song still runs  */
void sfx (unsigned char kind);

/* The effects, on or off - the `s` key, and off for the whole of a demo.
** Separate from music_mute because they are separate complaints: the
** clicks and thuds are the thing a spectator gets tired of, and the bed is
** the thing they are watching it for. */
void music_sfx (unsigned char on);
unsigned char music_sfx_on (void);

#endif
