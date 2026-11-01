@ECHO OFF
REM An event over the 16 KB buffer (scripts/boottest.py image/tests/bigevent.bat
REM --mock --expect "event too long" --expect "stop=end_turn"): the mock's
REM !bigevent sends a 20 KB text delta. CLAWDOS ends that reply with an error,
REM and the next message in the same run is answered normally.
ECHO CLAWDOS-BIGEVENT-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!bigevent"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "hello"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
