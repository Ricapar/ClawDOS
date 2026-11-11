@ECHO OFF
REM Retries that run out (scripts/boottest.py image/tests/busy.bat --mock
REM --expect "attempt 2 of 3); trying again" --expect "attempt 3 of 3)"
REM --reject "attempt 3 of 3); trying again" --reject "stop=end_turn"): the mock answers
REM !busy with HTTP 529 (retry-after 1) three times. CLAWDOS asks again twice, then shows
REM the error and ends the turn.
ECHO CLAWDOS-BUSY-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!busy"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
