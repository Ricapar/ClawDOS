@ECHO OFF
REM The 25-round cap (scripts/boottest.py image/tests/loop.bat --mock --timeout 240
REM --expect "loop: 25 tool calls, then tool_choice none" --expect "stop=end_turn"
REM --reject "VALIDATION:"): the mock's !loop calls sysinfo every round until a request
REM says tool_choice none. HARNESS allows 25 rounds, sends none on the 26th and the turn
REM ends with the mock's final answer.
ECHO CLAWDOS-LOOP-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -unsafe -m "!loop"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
