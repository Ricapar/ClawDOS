; JWasm real mode, MASM syntax: JWASMR -bin -Fo TJWASM.COM TJWASM.ASM
        .model tiny
        .code
        org 100h
start:  mov dx, offset msg
        mov ah, 9
        int 21h
        mov ax, 4C00h
        int 21h
msg     db 'JWASMR OK', 13, 10, '$'
        end start
