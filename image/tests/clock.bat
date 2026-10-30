@ECHO OFF
REM The clock from the bridge (scripts/boottest.py image/tests/clock.bat --bridge
REM --expect "Clock set from the bridge"): with the clock back at 1980, as on an
REM XT whose SNTP didn't take, CLAUDE sets it from the HELLO and logs the old time.
ECHO CLAWDOS-CLOCK-TEST > COM1
CALL C:\NET\NETUP.BAT
DATE 01-01-1980
TIME 00:00:00
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool sysinfo"
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAWDOS-TEST-DONE > COM1
