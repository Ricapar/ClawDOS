; STDINEOF.COM - the tool tour's probe for a program reading standard input
; under the run tool (lib/shell.c gives it NUL): reads handle 0 with INT 21h
; function 3Fh until end of file, then prints "STDIN EOF after N bytes" and
; exits with code 0 (a read error prints "STDIN read error", code 1). If
; stdin were the keyboard instead, it would wait forever.
cpu 8086
org 100h

        xor     si, si                  ; bytes read so far
again:  mov     ah, 3Fh
        xor     bx, bx                  ; standard input
        mov     cx, 128
        mov     dx, buf
        int     21h
        jc      failed
        or      ax, ax
        jz      eof
        add     si, ax
        jmp     again

eof:    mov     dx, said
        call    print
        mov     ax, si                  ; the count in decimal
        mov     di, digits + 5
        mov     bx, 10
next:   xor     dx, dx
        div     bx
        add     dl, '0'
        dec     di
        mov     [di], dl
        or      ax, ax
        jnz     next
        mov     dx, di
        call    print
        mov     dx, bytes
        call    print
        mov     ax, 4C00h
        int     21h

failed: mov     dx, error
        call    print
        mov     ax, 4C01h
        int     21h

print:  mov     ah, 9                   ; a '$'-terminated string
        int     21h
        ret

said    db      'STDIN EOF after $'
digits  db      '00000$'
bytes   db      ' bytes', 13, 10, '$'
error   db      'STDIN read error', 13, 10, '$'
buf:
