/* ------------------------------------------------------------------------
 * plus4.h - the machine, as UR FINKEL uses it.
 *
 * The Plus/4 in text mode is two parallel 40x25 byte matrices: a screen
 * matrix of character codes at $0C00 and a colour matrix at $0800.  A
 * colour byte is luminance*16 + hue, which is where the 16x8 palette comes
 * from, and it is the same encoding the BASIC edition POKEd by hand.
 *
 * The two matrices sit exactly $0400 apart, and every blitter in this
 * project leans on that: one pointer walks both, the colour pointer being
 * the screen pointer minus $0400.  Keeping that invariant is why nothing
 * here relocates the video matrix via TED $FF14.
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_PLUS4_H
#define URFINKEL_PLUS4_H

#define SCREEN_ADDR     0x0C00U
#define COLOR_ADDR      0x0800U
#define SCREEN_COLOR_D  0x0400U         /* screen - colour, always        */

#define SCR_W           40
#define SCR_H           25

#define SCREEN          ((unsigned char*)SCREEN_ADDR)
#define COLORMAP        ((unsigned char*)COLOR_ADDR)

/* Colour byte from luminance (0-7) and hue (0-15), the BASIC edition's
** `lum*16 + hue` written as a macro so the board tables read the same in
** both editions. */
#define CBYTE(lum, hue) ((unsigned char)(((lum) << 4) | (hue)))

/* --- the luminance band --------------------------------------------------
**
** TED's colour byte carries a 0-7 luminance, but the program does not use
** all of it, and it is better that the constraint is written down once
** than rediscovered per screen.
**
** The tiles the pieces stand on live between LUM_TILE_LO and LUM_TILE_HI.
** A player's own colour has to be legible against that AND against the
** gold of a rosette, which is why the picker offers a narrower band still:
** below LUM_PICK_MIN a piece sinks into the board whatever hue it is, and
** at 7 it competes with the rosette stars and the shimmer's highlight.
** Restricting the picker is not a limitation imposed on the player, it is
** the same range everything else in the program already obeys. */

#define LUM_TILE_LO     2               /* the darkest tile shadow        */
#define LUM_TILE_HI     7               /* a lit rosette                  */
#define LUM_PICK_MIN    3               /* what a player may choose       */
#define LUM_PICK_MAX    6

/* Screen codes the board is built from (same values as the BASIC source). */
#define CH_SOLID        160             /* reverse space: a solid cell    */
#define CH_STAR         170             /* reverse star: rosette fill     */
#define CH_SPACE        32
#define CH_RING         87              /* hollow ring: a waiting piece   */
#define CH_DISC         81              /* filled disc: a piece home      */
#define CH_PIECE_BASE   176             /* +n gives the reverse digit n   */
#define CH_REVERSE      128             /* OR into a letter to engrave it */
#define CH_DIAG_R       78              /* the lots' rising diagonal      */
#define CH_DIAG_L       77              /* ...and its falling twin        */
#define CH_RULE         93              /* the chronicle's left rule      */
#define CH_HATCH        102             /* half-shaded block: the frieze  */

/* --- TED, as absolute addresses so nothing depends on a struct layout -
**
** volatile is not decoration.  The raster counter changes underneath the
** program, and with -Osir cc65 will happily hoist a non-volatile read out
** of a spin loop and wait for a value that can now never arrive.  The
** write-side registers are volatile for the mirror-image reason: a store
** whose value is never read back must not be optimised away. */

#define TED_TIMER1_LO   (*(volatile unsigned char*)0xFF00)
#define TED_TIMER1_HI   (*(volatile unsigned char*)0xFF01)
#define TED_IRR         (*(volatile unsigned char*)0xFF09)  /* interrupt requests  */
#define TED_IMR         (*(volatile unsigned char*)0xFF0A)  /* masks + raster bit8 */
#define TED_RASTER_CMP  (*(volatile unsigned char*)0xFF0B)  /* raster compare 0-7  */
#define TED_S1FREQ_LO   (*(volatile unsigned char*)0xFF0E)
#define TED_S2FREQ_LO   (*(volatile unsigned char*)0xFF0F)
#define TED_S2FREQ_HI   (*(volatile unsigned char*)0xFF10)  /* bits 0-1            */
#define TED_SNDCTL      (*(volatile unsigned char*)0xFF11)  /* volume + enables    */
#define TED_MISC        (*(volatile unsigned char*)0xFF12)  /* ch1 freq hi + video */
#define TED_CHARADDR    (*(volatile unsigned char*)0xFF13)
#define TED_VIDEOADDR   (*(volatile unsigned char*)0xFF14)
#define TED_BGCOLOR     (*(volatile unsigned char*)0xFF15)
#define TED_BORDER      (*(volatile unsigned char*)0xFF19)
#define TED_RASTER_HI   (*(volatile unsigned char*)0xFF1C)  /* current raster bit8 */
#define TED_RASTER_LO   (*(volatile unsigned char*)0xFF1D)  /* current raster 0-7  */

/* $FF11 - the sound control register.  Note that the volume is GLOBAL:
** four bits shared by both voices.  There is no per-channel volume on
** TED, which is the single most important fact about writing music for
** this machine (see docs/music.md). */
#define SND_VOL_MASK    0x0F
#define SND_CH1_ON      0x10
#define SND_CH2_TONE    0x20
#define SND_CH2_NOISE   0x40

/* $FF0A / $FF09 */
#define IRQ_RASTER      0x02

/* KERNAL CHROUT codes for the two character sets.  The game is drawn in
** upper case / graphics, which is where the ring (87), the disc (81) and
** the reverse star (170) live; in the lower-case set those same codes are
** the letters w, q and *.  cc65's startup leaves the machine in lower
** case, so the renderer has to ask for the other set explicitly. */
#define CHARSET_UPPER   142
#define CHARSET_LOWER   14

/* PAL jiffies per second.  cc65 hardcodes CLOCKS_PER_SEC to 60 for CBM
** targets, which is the NTSC figure; VICE's xplus4 and the restored
** machine are both PAL, so timings in this project are divided by 50 -
** the same clock BASIC's TI counts. */
#define JIFFIES_PER_SEC 50

/* PAL raster lines per frame.  The music engine divides this into slots
** (see irq.s), which is what gives it a tick rate faster than the frame. */
#define RASTER_LINES    312

#endif
