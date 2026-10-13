@ECHO OFF
REM Boot smoke test. Results go to COM1, which boottest.py captures to a file.
ECHO CLAUDEXT-BOOT-OK > COM1
VER > COM1
MEM > COM1
C:\CLAUDEXT\HELLO.EXE > COM1
ECHO CLAUDEXT-TEST-DONE > COM1
