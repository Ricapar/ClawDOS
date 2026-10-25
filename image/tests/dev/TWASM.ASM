; Wolfware Asm: WASM TWASM (makes TWASM.COM)
        mov     dx, OFFSET msg
        mov     ah, 9
        int     21H
        mov     ax, 4C00H
        int     21H
msg     DB      'WASM OK', 13, 10, '$'
