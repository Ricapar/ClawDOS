@ECHO OFF
REM A thinking signature over the 16 KB event buffer (scripts/boottest.py
REM image/tests/bigsig.bat --mock --expect "big signature round trip: PASS"
REM --expect "stop=end_turn"): the mock's !bigsig sends a 24 KB signature_delta and a
REM tool call; HARNESS streams the signature into the history, and the mock
REM checks it came back whole in the request with the tool result.
ECHO CLAWDOS-BIGSIG-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -unsafe -m "!bigsig"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
