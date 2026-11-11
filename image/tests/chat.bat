@ECHO OFF
REM Plain chat through the native API client (scripts/boottest.py
REM image/tests/chat.bat --mock --expect "stop=end_turn"): one message to the
REM mock Messages API, then CLAWDOS.LOG to the serial port.
ECHO CLAWDOS-CHAT-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "hello"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
