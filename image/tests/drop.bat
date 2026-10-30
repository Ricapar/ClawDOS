@ECHO OFF
REM A lost link (scripts/boottest.py image/tests/drop.bat --bridge --expect
REM "resumed conversation" --expect "XT: Link lost"): the mock bridge's !drop
REM hangs up mid-reply. With -q, CLAWDOS reconnects once, resuming its
REM conversation and reporting the loss, then exits with an error.
ECHO CLAWDOS-DROP-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -q -m "!drop"
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAWDOS-TEST-DONE > COM1
