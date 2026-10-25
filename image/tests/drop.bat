@ECHO OFF
REM A lost link (scripts/boottest.py image/tests/drop.bat --bridge --expect
REM "connection #2" --expect "exit code 1"): the mock bridge's !drop hangs up
REM mid-reply. With -q, CLAUDE reconnects once (the bridge logs connection #2)
REM and then exits with an error instead of hanging.
ECHO CLAUDEXT-DROP-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAUDEXT\CLAUDE.EXE 10.0.2.2:18023 -q -m "!drop"
ECHO CLAUDE.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAUDEXT-TEST-DONE > COM1
