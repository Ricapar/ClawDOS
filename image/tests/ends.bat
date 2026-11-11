@ECHO OFF
REM Replies that end oddly (scripts/boottest.py image/tests/ends.bat --mock
REM --expect "stop=refusal" --expect "The model declined" --expect "stop=max_tokens"
REM --expect "reply cut off"): the mock's !refuse is a refusal, which CLAWDOS drops from
REM the history, and !cutoff a reply that hits max_tokens, which it keeps. (!error is in
REM streamerr.bat.)
ECHO CLAWDOS-ENDS-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!refuse"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!cutoff"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
