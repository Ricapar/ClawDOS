@ECHO OFF
REM Tool calls end to end (scripts/boottest.py image/tests/tools.bat --mock
REM --expect "tool tour: PASS"): the mock API's !tooltour calls every tool on
REM this machine and logs the verdict.
ECHO CLAWDOS-TOOLS-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tooltour"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAWDOS-TEST-DONE > COM1
