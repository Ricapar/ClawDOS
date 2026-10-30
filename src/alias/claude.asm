; CLAUDE.COM: the old name of ClawDOS. Runs C:\CLAWDOS\CLAWDOS.EXE with the same
; command tail, so typing CLAUDE still works. (A .BAT would mangle quoted
; arguments: FreeCOM splits and rejoins them at %1..%9.)
cpu 8086
org 0x100

        mov     sp, stk_top             ; stack at 1 KB; hand the rest back to DOS
        mov     bx, 0x40                ; 1 KB, in paragraphs
        mov     ah, 0x4A
        int     0x21
        mov     [pb + 4], cs            ; command tail = our PSP's, verbatim
        mov     [pb + 8], cs            ; FCBs, unused but expected
        mov     [pb + 12], cs
        mov     [save_ss], ss
        mov     [save_sp], sp
        mov     dx, target
        mov     bx, pb
        mov     ax, 0x4B00
        int     0x21
        mov     ss, [save_ss]           ; DOS 2.x trashes the stack on EXEC
        mov     sp, [save_sp]
        mov     ax, 0x4C01
        jc      .exit                   ; couldn't start it
        mov     ah, 0x4D                ; child's exit code
        int     0x21
        mov     ah, 0x4C
.exit:  int     0x21

target  db      'C:\CLAWDOS\CLAWDOS.EXE', 0
pb      dw      0                       ; environment: inherit ours
        dw      0x80, 0                 ; command tail offset, segment (set above)
        dw      0x5C, 0                 ; FCB 1
        dw      0x6C, 0                 ; FCB 2
save_ss dw      0
save_sp dw      0
stk_top equ     0x400                   ; the code and data above fit well below this
