@ECHO OFF
REM The launcher (scripts/boottest.py image/tests/relaunch.bat --mock --timeout 150
REM --expect "tool tour: PASS" --expect "Largest executable program size"): CLAW.EXE
REM starts CLAWDOS and, for every run, takes the program while CLAWDOS is out of
REM memory, then starts it again on the same conversation. MEM run that way shows
REM what a program gets; the tool tour then goes through two such relaunches.
ECHO CLAWDOS-RELAUNCH-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\CLAW.EXE 10.0.2.2:18023 -unsafe -q -m "!run MEM /C"
ECHO CLAW.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\RUN.OUT > COM1
C:\CLAWDOS\CLAW.EXE 10.0.2.2:18023 -unsafe -q -m "!tooltour"
ECHO CLAW.EXE tour exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
