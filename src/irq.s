; ---------------------------------------------------------------------------
; irq.s - the small program that runs between the lines.
;
; A raster interrupt fires four times per frame (PAL: 200 Hz) and does the
; sub-frame music work: it advances BOTH arpeggios one step, writes their
; two voices, and copies the level the C side asked for.  Everything it
; needs is in tables that the C side fills in at its leisure; the handler
; itself makes no decisions, which is what keeps it short enough to live in
; the border.
;
; It is assembly and not C for a hard reason: cc65 compiles to a software
; stack with shared zero-page temporaries, so calling a C function from an
; interrupt corrupts whatever C was doing in the main line.  The division
; of labour is therefore fixed - the interrupt writes registers, the main
; line decides what they should say.
;
; It does NOT modulate the volume.  TED has two tone voices and a single
; four-bit level shared between them, so every modulation of that register
; moves everything sounding at once; the hurdy-gurdy buzz this file used to
; run is gone for that reason, and the note over `volume:` records what was
; tried.  Expression is in pitch.
;
; --- WHICH VECTOR, AND WHY IT IS NOT $0314 --------------------------------
;
; This handler hangs off the 6502's own vector at $FFFE, in RAM.  It used to
; hang off the KERNAL's $0314, which is the ordinary way to do this on a
; Commodore machine, and on THIS toolchain that was silently fatal.
;
; The Plus/4 banks $8000-$FFFF between RAM and ROM with $FF3E/$FF3F.  Writes
; always land in RAM; READS come from whichever is switched in.  cc65's
; plus4 runtime therefore runs the whole program with RAM switched in and
; puts its own stub at the RAM $FFFE - and that stub, before it reaches
; $0314, switches the ROM back IN so that the KERNAL's interrupt code can
; run.  A handler on $0314 is entered with the BASIC ROM covering
; $8000-$BFFF.
;
; cc65's linker puts BSS at the end of the program.  Once UR FINKEL passed
; about 30 KB that landed above $8000 - so every variable on this page was
; being READ FROM BASIC ROM and written to the RAM underneath it.  `inc
; _music_frames` read the ROM byte at its address, added one, and stored
; that; the counter therefore sat at ROM+1 for ever, which is the "the
; interrupt stops after 34 frames" symptom.  The confirmation is exact:
; music_frames read 33 and basic-318006-01.bin holds 32 at that address.
; Everything else the handler owns was equally fictional - the arpeggio
; tables, the slot counter, and miscshdw, whose garbage went straight into
; $FF12 and is where the character-generator corruption came from.
;
; Because it depends on where the linker happens to put BSS, it appeared
; and disappeared as unrelated code was added, which is why it read as a
; timing fault for so long.
;
; Taking $FFFE directly means the handler is entered with RAM still switched
; in, so its code, its tables and its variables are all the ones it wrote.
; The rule this is an instance of, and it applies to any interrupt on this
; toolchain: DO NOT LET A HANDLER OF OURS BE ENTERED THROUGH THE KERNAL,
; because the KERNAL is not there unless the ROM is switched in, and while
; the ROM is switched in our own data is not there either.
;
; The one thing this arrangement requires of the rest of the program: the
; ROM must not be switched in while interrupts are enabled, or an interrupt
; would vector through the ROM's $FFFE and never reach us.  cc65's KERNAL
; wrappers do exactly that, so no KERNAL call may be made once the handler
; is installed.  The program makes one, cbm_k_bsout in board_init, and it
; is made before music_init.
; ---------------------------------------------------------------------------

        .export         _irq_install, _irq_remove
        .export         _arp_lo, _arp_hi, _arp_len, _arp_rate, _arp_gate
        .export         _arp2_lo, _arp2_hi, _arp2_len, _arp2_rate, _arp2_gate
        .export         _arp_sync
        .export         _snd_base_vol, _snd_enable, _music_frames
        .export         _sfx_lo, _sfx_hi, _sfx_ticks

TED_IRR     = $FF09
TED_IMR     = $FF0A
TED_RASCMP  = $FF0B
TED_S1LO    = $FF0E
TED_S2LO    = $FF0F
TED_S2HI    = $FF10             ; bits 0-1 only; the rest are unused, so
                                ; unlike $FF12 this one is safe to write
                                ; whole and needs no shadow
TED_SNDCTL  = $FF11
TED_MISC    = $FF12

; The 6502's own vector, in RAM under the KERNAL.  See the note above.
IRQVEC      = $FFFE

        .bss

; SIXTEEN steps, not eight.  A bar is sixteen sixteenth-notes and the
; figure is written across the whole bar, so the table is a bar long.  A
; LENGTH OF ONE is how a held note is expressed: the index never moves off
; step 0 and the voice sustains whatever is there, which is what the organ
; tone under the figure is.
_arp_lo:        .res    16      ; the figure, as TED frequency values
_arp_hi:        .res    16
_arp_len:       .res    1       ; steps in use; 1 = a sustained note
; Ticks per step.  One rate for the figure is right again now that the
; figure IS a run of sixteenths by construction - the long note values live
; on the other voice, as a held table of length one, rather than as varying
; durations inside a figure.  A sixteenth is 40 ticks at 75 bpm.
_arp_rate:      .res    1
; RESTS.  One byte per step: non-zero sounds, zero is silent.  Without this
; a voice can only ever change note, never stop, and a texture that never
; stops has no rhythm in it - it is one continuous sound whose pitch moves.
; Silencing is done by clearing the voice's enable bit for the tick rather
; than by touching the volume, because the volume is global and dropping it
; would silence the other voice too.
_arp_gate:      .res    16
; Voice 2 arpeggiates as well, from the same chord as voice 1.  It used to
; hold a single sustained drone; two moving voices out of one chord is a
; richer texture and costs the same handful of cycles.
_arp2_lo:       .res    16
_arp2_hi:       .res    16
_arp2_len:      .res    1       ; 0 = leave voice 2 alone (written songs)
_arp2_rate:     .res    1
_arp2_gate:     .res    16
; Set by the C side on a downbeat.  The handler clears it and restarts BOTH
; figures from step 0, so the two voices state the new chord together
; rather than each arriving at it whenever its own counter happens to wrap.
_arp_sync:      .res    1
_snd_base_vol:  .res    1       ; the level, written whole by the C side
_snd_enable:    .res    1       ; $10 voice 1, $20 voice 2 tone
_music_frames:  .res    1       ; bumped once a frame, for the C sequencer
_sfx_lo:        .res    1       ; an effect note, which pre-empts voice 1
_sfx_hi:        .res    1
_sfx_ticks:     .res    1

old_irq:        .res    2
installed:      .res    1       ; so old_irq is captured exactly once
; Scratch for the handler.  Deliberately NOT cc65's zero-page tmp1: the
; main line uses those locations, an interrupt is by definition reentrant
; with respect to them, and borrowing one corrupts whatever C was in the
; middle of.  One byte of absolute addressing is a cheap price for that
; not being true.
hitmp:          .res    1
; A shadow of TED_MISC's upper bits, taken ONCE at install.  $FF12 carries
; voice 1's top two frequency bits in 0-1 and the character generator's
; configuration in the rest, so writing it means preserving the rest - and
; a read-modify-write 200 times a second is 200 chances a second to latch
; a bad read into the character base.  Nothing here changes video mode, so
; the upper bits are constant and reading them once is enough.
miscshdw:       .res    1
slot:           .res    1
arp_idx:        .res    1
arp_cnt:        .res    1
arp2_idx:       .res    1
arp2_cnt:       .res    1
miscval:        .res    1       ; what $FF12 should say; written in slot 3
; Which voices are allowed to sound on THIS tick.  Rebuilt every interrupt
; from the two gate tables and ANDed into the sound control register at the
; end, so both voices' rests land on exactly the same tick.
gatemask:       .res    1

        .rodata

; Four evenly spread raster lines.  All below 256, so the raster compare's
; ninth bit (TED_IMR bit 0) can stay clear and never needs touching.
slottab:        .byte   20, 98, 176, 254

        .code

; ---------------------------------------------------------------------------
; void irq_install (void)
; ---------------------------------------------------------------------------
.proc   _irq_install

        sei
        ; Saved only on the FIRST install.  kbtest installs and removes the
        ; handler from a key, and a second save would record our own
        ; address as the one to chain to - after which anything that was
        ; not ours would jump to us, find it still was not ours, and jump
        ; to us again for ever.
        lda     installed
        bne     armed
        lda     IRQVEC
        sta     old_irq
        lda     IRQVEC+1
        sta     old_irq+1
        lda     #1
        sta     installed
armed:
        lda     #<handler
        sta     IRQVEC
        lda     #>handler
        sta     IRQVEC+1

        lda     #0
        sta     slot
        sta     arp_idx
        sta     arp_cnt
        sta     arp2_idx
        sta     arp2_cnt

        lda     TED_MISC        ; snapshot the video bits, once, ever
        and     #$FC
        sta     miscshdw

        lda     slottab
        sta     TED_RASCMP
        lda     TED_IMR
        and     #$FE            ; raster compare bit 8 = 0
        ora     #$02            ; raster interrupt enable
        sta     TED_IMR
        cli
        rts

.endproc

; ---------------------------------------------------------------------------
; void irq_remove (void)
; ---------------------------------------------------------------------------
.proc   _irq_remove

        sei
        lda     TED_IMR
        and     #$FD            ; raster interrupt off
        sta     TED_IMR
        lda     installed
        beq     done            ; never installed
        lda     old_irq
        sta     IRQVEC
        lda     old_irq+1
        sta     IRQVEC+1
done:   lda     #$00
        sta     TED_SNDCTL      ; silence both voices
        cli
        rts

.endproc

; ---------------------------------------------------------------------------
; The handler itself.  It is now the FIRST thing the processor reaches, so
; it saves the registers itself rather than finding them already pushed by
; somebody else's stub.
; ---------------------------------------------------------------------------
handler:
        pha
        txa
        pha
        tya
        pha
        ; Decimal mode is not cleared by the processor on an interrupt, and
        ; every count below is a binary one.
        cld

        ; BRK arrives on this same vector.  Hand it on unread: cc65's stub
        ; is what knows about $0316, and swallowing a breakpoint here would
        ; RTI back into the middle of the instruction that raised it.
        tsx
        lda     $0104,x         ; the status word the processor pushed
        and     #$10
        beq     notbrk
        jmp     notours         ; out of branch range; the tail is long
notbrk:
        lda     TED_IRR
        and     #$02            ; was this ours?
        bne     isours
        jmp     notours
isours:
        ; ---- move to the next raster slot ------------------------------
        ldx     slot
        inx
        cpx     #4
        bcc     :+
        ldx     #0
        inc     _music_frames   ; one bump per frame, for the sequencer
:       stx     slot
        lda     slottab,x
        sta     TED_RASCMP

        ; ---- and ONLY NOW acknowledge ----------------------------------
        ; The compare is moved off this line before the flag is cleared,
        ; which is the safe order rather than a measured fix: clearing the
        ; flag while the raster is still on the line that raised it invites
        ; the same line to raise it again.  Measured either way this build
        ; takes four interrupts a frame, so nothing here was observed to be
        ; wrong - but the handler is now reached straight off $FFFE instead
        ; of through two stubs and the KERNAL, so it runs early enough in
        ; the line for the question to arise at all, and this way round it
        ; cannot.
        lda     #$02
        sta     TED_IRR

        lda     #$FF            ; both voices open until a gate says not
        sta     gatemask

        ; ---- the downbeat -----------------------------------------------
        ; The C side raises _arp_sync when the bar turns over and a new
        ; chord has been loaded.  Both figures go back to step 0 here, in
        ; the same tick, so the downbeat is the two voices starting the
        ; chord together rather than drifting into it independently.
        lda     _arp_sync
        beq     nosync
        lda     #0
        sta     _arp_sync
        sta     arp_idx
        sta     arp2_idx
        lda     _arp_rate
        sta     arp_cnt
        lda     _arp2_rate
        sta     arp2_cnt
nosync:

        ; ---- voice 1: an effect if one is sounding, else the arpeggio --
        ; An effect takes the VOICE and nothing else.  It used to take the
        ; volume register too, which is how it was given priority; that is
        ; gone, because the register is global and raising it for an effect
        ; raises the drone, the bed and everything else along with it.
        lda     _sfx_ticks
        beq     arpeggio
        dec     _sfx_ticks
        lda     _sfx_lo
        sta     TED_S1LO
        lda     _sfx_hi
        jmp     sethi

arpeggio:
        lda     _arp_len
        beq     voice2          ; nothing on voice 1; voice 2 still runs
        ; Counted to zero rather than tested with BPL: BPL only works while
        ; the count stays under 128 and a rate may exceed it.
        lda     arp_cnt
        beq     :+
        dec     arp_cnt
        jmp     playstep
:       ldy     arp_idx
        iny
        cpy     _arp_len
        bcc     :+
        ldy     #0
:       sty     arp_idx
        lda     _arp_rate
        sta     arp_cnt

playstep:
        ldy     arp_idx
        lda     _arp_gate,y
        bne     :+
        lda     gatemask
        and     #$EF            ; voice 1 rests this step
        sta     gatemask
:       lda     _arp_lo,y
        sta     TED_S1LO
        lda     _arp_hi,y

sethi:
        ; Voice 1's top two frequency bits, merged with the video bits
        ; snapshotted at install.  Deliberately NOT a read-modify-write of
        ; $FF12 - see miscshdw.
        and     #$03
        ora     miscshdw
        sta     miscval

        ; ---- $FF12 is written ONLY in the bottom border ----------------
        ; $FF12 carries voice 1's top two frequency bits AND the character
        ; generator's configuration.  Writing it while the raster is inside
        ; the visible display disturbs the character fetch for the rest of
        ; that line, and the rows it damages depend on exactly when the
        ; handler runs - which is why the corruption moved with the linker
        ; layout and looked like memory being scribbled on.
        ;
        ; Slot 3 is raster line 254, below the visible picture, so the write
        ; lands in the border.  Voice 1's pitch therefore updates at 50 Hz
        ; rather than 200 - which costs nothing, because a figure step is
        ; ten frames long.
        lda     slot
        cmp     #3
        bne     voice2
        lda     miscval
        sta     TED_MISC

        ; ---- voice 2: the second arpeggio ------------------------------
        ; Same shape as voice 1 and its own counter, so the two figures can
        ; run at different rates over the same chord.  _arp2_len is left at
        ; zero while a written song is playing, because a song drives voice
        ; 2 note by note from the C side and the two must not both write it.
voice2:
        lda     _arp2_len
        beq     volume
        lda     arp2_cnt
        beq     :+
        dec     arp2_cnt
        jmp     play2
:       ldy     arp2_idx
        iny
        cpy     _arp2_len
        bcc     :+
        ldy     #0
:       sty     arp2_idx
        lda     _arp2_rate
        sta     arp2_cnt

play2:
        ldy     arp2_idx
        lda     _arp2_gate,y
        bne     :+
        lda     gatemask
        and     #$DF            ; voice 2 rests this step
        sta     gatemask
:       lda     _arp2_lo,y
        sta     TED_S2LO
        lda     _arp2_hi,y
        and     #$03
        sta     TED_S2HI

        ; ---- the global volume -----------------------------------------
        ; Written straight through, with nothing added to it here.
        ;
        ; This used to be the interesting part of the file: a buzz table
        ; subtracted an attenuation every few ticks, which over a drone is
        ; what a hurdy-gurdy's bridge does, and effects overrode the level
        ; outright.  Both are gone.  The register is GLOBAL - one four-bit
        ; level for both voices - so every modulation of it modulates
        ; everything sounding, and what that produces is a texture that
        ; pumps as a whole.  The C sequencer now owns the level entirely,
        ; it only ever moves it downwards, and the interrupt's job here is
        ; to copy it.
volume:
        lda     _snd_base_vol
        and     #$0F
        ora     _snd_enable     ; $10 voice 1, $20 voice 2 tone
        and     gatemask        ; ...less whichever of them is resting
        sta     TED_SNDCTL

        pla
        tay
        pla
        tax
        pla
        rti

        ; Not ours - or a BRK.  The registers go back exactly as they were
        ; found, because whatever is on the other end of old_irq expects to
        ; be entered by the processor and will push them itself.
notours:
        pla
        tay
        pla
        tax
        pla
        jmp     (old_irq)
