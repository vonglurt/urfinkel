/* ------------------------------------------------------------------------
 * kbd.h - the keyboard, read from TED rather than asked of the KERNAL.
 *
 * WHY THIS EXISTS.  `docs/keyboard-report.md` records the measurement:
 * under cc65's plus4 runtime the KERNAL's periodic interrupt does not run
 * - its jiffy clock at $A3-$A5 does not advance with our raster interrupt
 * installed OR removed, while our own handler counts frames normally.  That
 * same interrupt is what scans the matrix and fills the buffer count at
 * $EF, and cc65's kbhit() reads $EF.  So kbhit() can never return true, and
 * no key could ever reach the game.
 *
 * The machine is not at fault and neither is irq.s.  On a Plus/4 the
 * keyboard is scanned by TED itself - there is no CIA at $DC00 as on a C64
 * - so the matrix can be read directly, without the KERNAL being involved
 * at all.  That is what this module does.
 *
 * THE PROTOCOL, confirmed on the machine by src/kbhunt.c:
 *
 *     write the row mask (active low) to $FD30
 *     strobe $FF08 by writing to it
 *     read $FF08 back for that row's columns, active low
 *
 * Six keys were captured and every one matched the documented matrix -
 * 1 and 2 on row 7 bits 0 and 3, 3 and 4 on row 1 bits 0 and 3, 5 on row 2
 * bit 0, m on row 4 bit 4 - which is what licenses using the whole table
 * below rather than only the keys that were pressed.
 * --------------------------------------------------------------------- */

#ifndef URFINKEL_KBD_H
#define URFINKEL_KBD_H

/* Sample the idle matrix and learn which lines are stuck.  Call once, at
** boot, before anything is typed. */
void kbd_init (void);

/* One scan of the matrix.  Call this ONCE A FRAME and from nowhere else -
** wait_frames_live does it, which is every wait in the game.
**
** The rate is not an implementation detail, it is the debounce: a key
** counts as down only once it has read down on two consecutive scans, and
** that is a length of time only if the scans are evenly spaced.  When the
** scanning lived inside kbd_get it ran at whatever rate the caller's loop
** happened to have - microseconds apart in wait_key's tight spin, a third
** of a second apart in the menu - which produced phantom keys at one end
** and dead keys at the other.  See the note over kbd_scan. */
void kbd_scan (void);

/* The next key pressed since it was last called, or 0.  Edge triggered -
** holding a key yields it once - and buffered, so keys pressed during an
** animation are still there when the game next asks. */
unsigned char kbd_get (void);

#endif
