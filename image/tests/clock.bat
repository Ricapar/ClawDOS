@ECHO OFF
REM The clock from NTP (scripts/boottest.py image/tests/clock.bat --expect "System time set"
REM --expect "PING after the clock: 0"): with the clock back at 1980, as on a PC
REM without a battery clock, NETUP.BAT sets it by SNTP (needs the internet) and
REM takes a fresh DHCP lease, so mTCP programs still run after the jump.
ECHO CLAWDOS-CLOCK-TEST > COM1
DATE 01-01-1980
TIME 00:00:00
CALL C:\NET\NETUP.BAT
TYPE C:\NET\SNTP.LOG > COM1
DATE /T > COM1
C:\NET\PING.EXE -count 1 10.0.2.2 > NUL
ECHO PING after the clock: %ERRORLEVEL% > COM1
ECHO CLAWDOS-TEST-DONE > COM1
