@ECHO OFF
REM The launcher (scripts/boottest.py image/tests/relaunch.bat --mock --timeout 150
REM --expect "tool tour: PASS" --expect "Largest executable program size"): CLAW.EXE
REM starts CLAWDOS and, for every run, takes the program while CLAWDOS is out of
REM memory, then starts it again on the same conversation. MEM run that way shows
REM what a program gets; the tool tour then goes through two such relaunches.
REM The clock is back at 1980 before DHCP runs, as on a PC without a clock:
REM CLAWDOS sets it from the API, and the resumed CLAWDOS must still pass
REM mTCP's DHCP lease check (lib/mtcpcfg.h).
ECHO CLAWDOS-RELAUNCH-TEST > COM1
DATE 01-01-1980
TIME 00:00:00
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\CLAW.EXE 10.0.2.2:18023 -unsafe -q -m "!run MEM /C"
ECHO CLAW.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\RUN.OUT > COM1
C:\CLAWDOS\CLAW.EXE 10.0.2.2:18023 -unsafe -q -m "!tooltour"
ECHO CLAW.EXE tour exit code %ERRORLEVEL% > COM1
IF NOT EXIST C:\CLAWDOS\RESUME.TXT ECHO resume state consumed > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
