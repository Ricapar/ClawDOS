@ECHO OFF
REM Tool calls end to end (scripts/boottest.py image/tests/tools.bat --bridge
REM --expect "tool tour: PASS"): the mock bridge's !tooltour calls every tool on
REM this machine and logs the verdict.
ECHO CLAWDOS-TOOLS-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!tooltour"
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAWDOS-TEST-DONE > COM1
