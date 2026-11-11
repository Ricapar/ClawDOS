@ECHO OFF
REM A tool call cut by max_tokens inside its input (scripts/boottest.py
REM image/tests/cutcall.bat --mock, the expectations in the Makefile). The
REM mock's !cutcall streams a write call whose input_json_delta stops inside a
REM string, then stop_reason max_tokens. The harness must store the call with
REM "input":{} (not the half of a JSON object it received) and answer it with
REM an error result, so the chat stays valid: it is reopened and a second
REM message goes out under the validator, and !cutcheck finds the call and its
REM error result as they should be ("cut call round trip: PASS").
ECHO CLAWDOS-CUTCALL-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!cutcall"
ECHO HARNESS.EXE cut exit code %ERRORLEVEL% > COM1
REN C:\CLAWDOS\CHATS\*.JSN CUT.JSN
REN C:\CLAWDOS\CHATS\*.PRE CUT.PRE
TYPE C:\CLAWDOS\CHATS\CUT.JSN > COM1
ECHO. > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c CUT.JSN -unsafe -q -m "!cutcheck"
ECHO HARNESS.EXE check exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
