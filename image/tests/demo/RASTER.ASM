; Polled raster bars, 320x200 mode, RGBI: TINYASM -f bin RASTER.ASM -o RASTER.COM
; Per scanline: wait for display enable, then for the horizontal blank
; (3DAh bit 0), then write the next colour to the colour register (3D9h).
; In mode 4 that register is the background AND border colour, so each bar
; is the full width of the screen. 1800 frames (30 s) still, then 1800 moving.
        org 0x100
start:  cld
        mov ax, 0x0004
        int 0x10
        mov di, tbl             ; tbl = pat repeated 8 times (256 bytes)
        mov dx, 8
mkrep:  mov si, pat
        mov cx, 32
        rep movsb
        dec dx
        jnz mkrep
        mov word [frames], 3600
        xor bp, bp              ; bar phase, 0..31
frame:  mov dx, 0x3da
vs1:    in al, dx               ; wait for vertical retrace...
        test al, 8
        jz vs1
vs2:    in al, dx               ; ...and its end: top border now
        test al, 8
        jnz vs2
        cli                     ; a timer tick mid-frame would tear a line
        lea si, [bp+tbl]
        mov cx, 200
line:   lodsb
        mov ah, al
de:     in al, dx               ; wait while not displaying
        test al, 1
        jnz de
hb:     in al, dx               ; wait for this line's horizontal blank
        test al, 1
        jz hb
        mov al, ah
        mov dl, 0xd9
        out dx, al
        mov dl, 0xda
        loop line
        xor al, al              ; black for the bottom border
        mov dl, 0xd9
        out dx, al
        sti                     ; the missed timer tick is taken here
        cmp word [frames], 1800
        ja still
        inc bp
        and bp, 31
still:  dec word [frames]
        jnz frame
        mov ax, 0x0003
        int 0x10
        mov dx, msg
        mov ah, 9
        int 0x21
        mov ax, 0x4c00
        int 0x21
msg:    db "RASTER done", 13, 10, "$"
pat:    db 0, 1, 1, 9, 9, 11, 15, 11, 9, 9, 1, 1, 0, 0, 0, 0
        db 0, 4, 4, 12, 12, 14, 15, 14, 12, 12, 4, 4, 0, 0, 0, 0
frames: dw 0
tbl:
