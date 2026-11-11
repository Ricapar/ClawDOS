@ECHO OFF
REM Replies that end oddly (scripts/boottest.py image/tests/ends.bat --mock
REM --expect "stop=refusal" --expect "The model declined" --expect "stop=max_tokens"
REM --expect "reply cut off" --expect "\"content\":\"!cutoff\"" --reject "\"content\":\"!refuse\""):
REM the mock's !refuse is a refusal, which CLAWDOS drops from the history, and !cutoff a
REM reply that hits max_tokens, which it keeps; the conversation files go to the serial
REM port to show it. (!error is in streamerr.bat.)
ECHO CLAWDOS-ENDS-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!refuse"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!cutoff"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
FOR %%F IN (C:\CLAWDOS\CHATS\*.JSN) DO TYPE %%F > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
