@ECHO OFF
REM An overload inside the stream (scripts/boottest.py image/tests/overload.bat --mock
REM --expect "before any text); trying again" --expect "stop=end_turn"): the mock
REM answers !overload once with a 200 whose stream is an overloaded_error event
REM before any text; CLAWDOS retries it as it would an HTTP 529 and completes.
ECHO CLAWDOS-OVERLOAD-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!overload"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
