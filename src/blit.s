; ---------------------------------------------------------------------------
; blit.s - the primitive the whole renderer stands on.
;
; blit_run writes one horizontal run of identical cells into BOTH the screen
; matrix and the colour matrix in a single pass.  It exists because that is
; the shape of every drawing operation in this game: a button is four runs,
; a gutter is one cell, a plaque row is one run, a screen clear is 25.
;
; The colour pointer is never passed in.  The two matrices are $0400 apart
; on the Plus/4 (screen $0C00, colour $0800), so the routine derives it by
; subtracting 4 from the high byte - one SBC instead of a second pointer to
; set up and carry around.
;
; The inner loop is 25 cycles per cell, against the BASIC edition's measured
; ~22 ms per POKE statement.
;
; Parameters are globals rather than stack arguments on purpose: cc65's
; stack-based calling convention costs more to unpack than this routine
; costs to run, and every caller sets the same three fields.
; ---------------------------------------------------------------------------

        .export         _blit_run
        .export         _blit_ptr, _blit_ch, _blit_cl
        .importzp       ptr1, ptr2, tmp1, tmp2

        .bss

_blit_ptr:      .res    2       ; destination in the SCREEN matrix
_blit_ch:       .res    1       ; screen code to write
_blit_cl:       .res    1       ; colour byte to write

        .code

; void __fastcall__ blit_run (unsigned char n);   n arrives in A
.proc   _blit_run

        tax                     ; count -> X
        beq     done            ; a zero-length run is legal and free

        lda     _blit_ptr
        sta     ptr1
        sta     ptr2            ; low byte is shared: the gap is exactly $0400
        lda     _blit_ptr+1
        sta     ptr1+1
        sec
        sbc     #$04            ; colour matrix = screen matrix - $0400
        sta     ptr2+1

        lda     _blit_ch        ; hoist both operands into zero page, so the
        sta     tmp1            ; loop body never touches absolute memory
        lda     _blit_cl
        sta     tmp2

        ldy     #$00
loop:   lda     tmp1
        sta     (ptr1),y
        lda     tmp2
        sta     (ptr2),y
        iny
        dex
        bne     loop

done:   rts

.endproc
