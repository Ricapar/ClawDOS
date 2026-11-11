@ECHO OFF
REM A redacted_thinking block (scripts/boottest.py image/tests/redacted.bat --mock
REM --expect "redacted thinking round trip: PASS" --expect "stop=end_turn"
REM --reject "VALIDATION:"): the mock's !redacted sends one ahead of a tool call, and
REM checks it came back unchanged in the request with the tool result.
ECHO CLAWDOS-REDACTED-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -unsafe -m "!redacted"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
