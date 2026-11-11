@ECHO OFF
REM Memory for in-harness runs (scripts/boottest.py image/tests/runmem.bat --mock
REM --timeout 240, the expectations in the Makefile). The harness lends its big
REM buffers to DOS while a run's program executes (lib/runmem.h), so MEM started
REM directly and through COMMAND.COM (C:\TESTS\MEMSHELL.BAT) must see at least
REM the floors !memrun is given: under HARNESS.EXE alone and under CLAWDOS.EXE.
REM !memgone has EATMEM stay resident with nearly all of that memory: the harness
REM cannot take its buffers back, so the turn ends after the result; EATMEM /F
REM frees it, and the chat reopens with the result last (the validator checks the
REM whole history). !memshort leaves 90 KB: a command that needs COMMAND.COM is
REM refused (it would hang at FreeCOM's strings prompt), MEM still runs, and
REM DOSUNIT, too big, fails with DOS's own error.
ECHO CLAWDOS-RUNMEM-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!memgone"
ECHO HARNESS.EXE memgone exit code %ERRORLEVEL% > COM1
C:\TESTS\EATMEM.EXE /F > COM1
REN C:\CLAWDOS\CHATS\*.JSN GONE.JSN
REN C:\CLAWDOS\CHATS\*.PRE GONE.PRE
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c GONE -unsafe -q -m "!reopened gone"
ECHO HARNESS.EXE gone exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!memrun 230 160"
ECHO HARNESS.EXE memrun exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!memrun 176 105"
ECHO CLAWDOS.EXE memrun exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!memshort"
ECHO HARNESS.EXE memshort exit code %ERRORLEVEL% > COM1
C:\TESTS\EATMEM.EXE /F > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
