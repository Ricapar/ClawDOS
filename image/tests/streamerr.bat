@ECHO OFF
REM Stream errors that are not retried (scripts/boottest.py image/tests/streamerr.bat --mock
REM --expect "API error 529: Overloaded (simulated)" --expect "Bad request (simulated" --reject "trying again"):
REM !error sends an overloaded_error event after some text, !badstream an
REM invalid_request_error before any; CLAWDOS ends each turn with the message.
ECHO CLAWDOS-STREAMERR-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!error"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!badstream"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
