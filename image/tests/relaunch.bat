@ECHO OFF
REM The launcher (scripts/boottest.py image/tests/relaunch.bat --mock --timeout 150
REM --expect "tool tour: PASS" --expect "Largest executable program size"): CLAWDOS.EXE
REM starts HARNESS and, for a run with full_memory, takes the program while the
REM harness is out of memory, then starts it again on the same conversation. MEM
REM run that way shows what a program gets; a plain run stays inside the harness
REM (no RUN.OUT); the tool tour then runs one in place and one through a relaunch.
REM The clock is back at 1980 before DHCP runs, as on a PC without a clock:
REM CLAWDOS sets it from the API, and the resumed CLAWDOS must still pass
REM mTCP's DHCP lease check (lib/mtcpcfg.h).
ECHO CLAWDOS-RELAUNCH-TEST > COM1
DATE 01-01-1980
TIME 00:00:00
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!bigrun MEM /C"
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\RUN.OUT > COM1
DEL C:\CLAWDOS\RUN.OUT
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!run VER"
IF NOT EXIST C:\CLAWDOS\RUN.OUT ECHO plain run stayed in the harness > COM1
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!tooltour"
ECHO CLAWDOS.EXE tour exit code %ERRORLEVEL% > COM1
IF NOT EXIST C:\CLAWDOS\RESUME.TXT ECHO resume state consumed > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
