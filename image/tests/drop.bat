@ECHO OFF
REM A lost link (scripts/boottest.py image/tests/drop.bat --mock --expect
REM "connection #2"): the mock API's !drop hangs up mid-reply. CLAWDOS drops the
REM half reply, reconnects and asks again, once; the second answer completes.
ECHO CLAWDOS-DROP-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!drop"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
