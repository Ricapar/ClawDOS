@ECHO OFF
REM Tool calls end to end (scripts/boottest.py image/tests/tools.bat --mock
REM --expect "tool tour: PASS" --expect "progress note: Tour step 1 of"): the mock
REM API's !tooltour calls every tool on this machine and logs the verdict. The
REM shipped settings ask for progress notes, so each response opens with one
REM naming the next step; CLAWDOS.LOG shows each that was drawn on the screen.
ECHO CLAWDOS-TOOLS-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tooltour"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
