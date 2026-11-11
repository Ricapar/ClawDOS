; QUADS.COM - the screenshot tests' picture (image/tests/shot.bat): mode 4
; (320x200, 4 colours) set through the BIOS, a blue background and palette 0
; bright (light green, light red, yellow; colour register 11h), and four
; solid quadrants in colours 0 1 / 2 3 (top left, top right / bottom left,
; bottom right), crossed by a one-pixel diagonal from (0,0) to (199,199)
; whose colour is the quadrant's XOR 3. Then it waits DELAY ticks and exits.
; QUADS.COM leaves mode 4 on the screen; QUADT.COM (built with -dRESTORE)
; waits longer and sets text mode again before it exits, as a demo does,
; so only shot_at can see it. QUAD6.COM (-dHIRES) draws the same quadrants'
; bytes in mode 6 (640x200, black and the BIOS's white): black, fine stripes,
; fine stripes, white, with the diagonal (y, y) inverted. TEXT40.COM
; (-dTEXT40) leaves a 40-column text screen (mode 1) with two lines on it.
; tunnel/test_mockapi.py draws the same pictures and checks the pixel digests
; the boot test expects.
cpu 8086
org 100h

%ifdef TEXT40
        mov     ax, 0001h               ; 40x25 colour text
        int     10h
        mov     ah, 9
        mov     dx, text40
        int     21h
        mov     ax, 4C00h
        int     21h
text40: db      'TEXT40: forty columns', 13, 10, 'second line of TEXT40', 13, 10, '$'
%else

%ifdef RESTORE
DELAY   equ     55                      ; ticks: 3 s
%else
DELAY   equ     18                      ; 1 s
%endif

%ifdef HIRES
        mov     ax, 0006h               ; mode 6, the BIOS's colours
        int     10h
%else
        mov     ax, 0004h               ; mode 4
        int     10h
        mov     ah, 0Bh                 ; background colour 1, bit 4: bright foreground
        mov     bx, 0011h
        int     10h
        mov     ah, 0Bh                 ; palette 0
        mov     bx, 0100h
        int     10h
%endif

        mov     ax, 0B800h
        mov     es, ax
        cld
        xor     bx, bx                  ; y
fill:   call    rowstart
        xor     al, al                  ; top: colours 0 and 1
        mov     ah, 55h
        cmp     bx, 100
        jb      top
        mov     al, 0AAh                ; bottom: colours 2 and 3
        mov     ah, 0FFh
top:    mov     cx, 40                  ; 160 pixels
        rep     stosb
        mov     al, ah
        mov     cx, 40
        rep     stosb
        inc     bx
        cmp     bx, 200
        jb      fill

        xor     bx, bx                  ; the diagonal: pixel (y, y)
diag:   call    rowstart
%ifdef HIRES
        mov     ax, bx
        mov     cl, 3
        shr     ax, cl                  ; byte x / 8
        add     di, ax
        mov     cl, bl
        and     cl, 7
        mov     al, 80h                 ; the leftmost pixel is the high bit
        shr     al, cl
%else
        mov     ax, bx
        mov     cl, 2
        shr     ax, cl                  ; byte x / 4
        add     di, ax
        mov     cl, bl
        and     cl, 3
        mov     al, 3
        sub     al, cl
        shl     al, 1
        mov     cl, al                  ; (3 - x mod 4) * 2: the leftmost pixel is in the high bits
        mov     al, 3
        shl     al, cl
%endif
        xor     [es:di], al
        inc     bx
        cmp     bx, 200
        jb      diag

        push    ds                      ; wait, by the BIOS tick count
        mov     ax, 40h
        mov     ds, ax
        mov     si, [6Ch]
waitlp: mov     ax, [6Ch]
        sub     ax, si
        cmp     ax, DELAY
        jb      waitlp
        pop     ds
%ifdef RESTORE
        mov     ax, 0003h
        int     10h
%endif
        mov     ax, 4C00h
        int     21h

; di = the start of row bx: even rows from 0, odd rows from 2000h, 80 bytes each
rowstart:
        mov     ax, bx
        shr     ax, 1
        mov     dx, 80
        mul     dx
        mov     di, ax
        test    bl, 1
        jz      .even
        add     di, 2000h
.even:  ret
%endif
