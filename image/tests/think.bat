@ECHO OFF
REM A thinking block with thinking_delta text (scripts/boottest.py image/tests/think.bat
REM --mock --expect "I should answer with a thinking block" --expect "stop=end_turn"
REM --reject "VALIDATION:"): the
REM mock's !think streams the text in pieces ahead of the signature, and the conversation
REM file written to the serial port must hold that text. Then !ping: ping events between
REM the text deltas, which CLAWDOS must ignore.
ECHO CLAWDOS-THINK-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!think"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!ping"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
FOR %%F IN (C:\CLAWDOS\CHATS\*.JSN) DO TYPE %%F > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
