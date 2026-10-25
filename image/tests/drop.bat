@ECHO OFF
REM A lost link (scripts/boottest.py image/tests/drop.bat --bridge --expect
REM "resumed conversation" --expect "XT: Link lost"): the mock bridge's !drop
REM hangs up mid-reply. With -q, CLAUDE reconnects once, resuming its
REM conversation and reporting the loss, then exits with an error.
ECHO CLAUDEXT-DROP-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAUDEXT\CLAUDE.EXE 10.0.2.2:18023 -q -m "!drop"
ECHO CLAUDE.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAUDEXT-TEST-DONE > COM1
