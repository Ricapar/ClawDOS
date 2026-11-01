@ECHO OFF
REM The clock from the API's Date header (scripts/boottest.py image/tests/clock.bat
REM --mock --expect "Clock set from the API"): with the clock back at 1980, as on a
REM DOS PC whose SNTP didn't take, CLAWDOS sets it from the first response and
REM logs the old time.
ECHO CLAWDOS-CLOCK-TEST > COM1
CALL C:\NET\NETUP.BAT
DATE 01-01-1980
TIME 00:00:00
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool sysinfo"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
