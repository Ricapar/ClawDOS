@ECHO OFF
REM Boot smoke test. Results go to COM1, which boottest.py captures to a file.
ECHO CLAWDOS-BOOT-OK > COM1
VER > COM1
MEM > COM1
C:\CLAWDOS\HELLO.EXE > COM1
ECHO CLAWDOS-TEST-DONE > COM1
