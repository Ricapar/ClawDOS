; TinyASM (NASM syntax): TINYASM -f bin TTINY.ASM -o TTINY.COM
        org 0x100
        mov dx, msg
        mov ah, 9
        int 0x21
        mov ax, 0x4c00
        int 0x21
msg:    db "TINYASM OK", 13, 10, "$"
