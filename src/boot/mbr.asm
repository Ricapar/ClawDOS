; ClawDOS master boot record.
; Relocates itself to 0000:0600, finds the active partition, loads its first
; sector to 0000:7C00 using CHS from the partition entry, and jumps to it with
; DL = boot drive and DS:SI -> partition entry (the classic IBM/MS-DOS contract).
; 8086-only; the partition table is filled in by scripts/fatimg.py.

        cpu     8086
        org     0x0600

start:
        cli
        xor     ax, ax
        mov     ss, ax
        mov     sp, 0x7C00
        mov     ds, ax
        mov     es, ax
        sti
        cld
        mov     si, 0x7C00
        mov     di, 0x0600
        mov     cx, 256
        rep     movsw
        jmp     0x0000:relocated

relocated:
        mov     si, ptable
        mov     cx, 4
.find:
        cmp     byte [si], 0x80
        je      .found
        add     si, 16
        loop    .find
        mov     si, msg_noactive
        jmp     fail

.found:
        mov     di, 5                   ; retries
.read:
        mov     ax, 0x0201              ; read 1 sector
        mov     bx, 0x7C00
        mov     dh, [si + 1]            ; head
        mov     cx, [si + 2]            ; sector | cylinder
        int     0x13                    ; DL still holds the BIOS boot drive
        jnc     .loaded
        xor     ax, ax                  ; reset and retry
        int     0x13
        dec     di
        jnz     .read
        mov     si, msg_readerr
        jmp     fail

.loaded:
        cmp     word [0x7DFE], 0xAA55
        jne     .nosig
        jmp     0x0000:0x7C00

.nosig:
        mov     si, msg_nosig

fail:
        lodsb
        or      al, al
        jz      .hang
        mov     ah, 0x0E
        xor     bx, bx
        int     0x10
        jmp     fail
.hang:
        sti
        hlt
        jmp     .hang

msg_noactive:   db 'No active partition', 0
msg_readerr:    db 'Disk read error', 0
msg_nosig:      db 'Missing boot signature', 0

        times 446 - ($ - $$) db 0
ptable: times 64 db 0
        dw      0xAA55
