@ECHO OFF
REM An overloaded API (scripts/boottest.py image/tests/retry.bat --mock --expect
REM "trying again" --expect "stop=end_turn"): the mock answers !retry with HTTP
REM 529 and retry-after once; CLAWDOS logs the wait, asks again and completes.
ECHO CLAWDOS-RETRY-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!retry"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
