@ECHO OFF
REM Follow-ups to devtools.bat (python scripts/xttest.py image/tests/devtool2.bat):
REM BCC wants lowercase .c names; Turbo C tiny model .COM; GW-BASIC output via a file.
SET L=C:\DEVTEST.LOG
C:
CD \TESTS\DEV
ECHO Claude-XT development tools test 2 > %L%

ECHO === BCC
ECHO === BCC >> %L%
ECHO.|TIME >> %L%
C:\DEVEL\BCC\BIN\BCC -o hbc.com hbc.c >> %L%
ECHO.|TIME >> %L%
IF EXIST HBC.COM HBC >> %L%

ECHO === TURBO C TINY
ECHO === TURBO C TINY >> %L%
ECHO.|TIME >> %L%
TCC -mt -lt -eHTCT.COM HTC.C >> %L%
ECHO.|TIME >> %L%
IF EXIST HTCT.COM HTCT >> %L%

ECHO === GWBASIC FILE
ECHO === GWBASIC FILE >> %L%
ECHO.|TIME >> %L%
GWBASIC TGWFILE.BAS
TYPE GWOUT.TXT >> %L%
ECHO.|TIME >> %L%
ECHO DEVTEST-DONE >> %L%
ECHO DEVTEST-DONE
