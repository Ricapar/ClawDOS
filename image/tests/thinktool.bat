@ECHO OFF
REM Thinking text that needs JSON escaping (scripts/boottest.py image/tests/thinktool.bat
REM --mock --expect "thinking round trip: PASS" --expect "stop=end_turn" --reject "VALIDATION:"):
REM the mock's !thinktool streams thinking_delta text with a quote, a backslash and
REM non-ASCII letters, then a tool call; HARNESS stores it in the history and sends it
REM back with the tool result, where the mock checks it character for character.
ECHO CLAWDOS-THINKTOOL-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -unsafe -m "!thinktool"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
