@ECHO OFF
REM Development tools on the 8088 (python scripts/test8088.py image/tests/devtools.bat):
REM builds and runs a hello program with each tool in C:\TESTS\DEV, timing each
REM step into C:\DEVTEST.LOG. Riskiest last: if a tool hangs, the log shows where.
SET L=C:\DEVTEST.LOG
C:
CD \TESTS\DEV
ECHO ClawDOS development tools test > %L%

ECHO === TINYASM
ECHO === TINYASM >> %L%
ECHO.|TIME >> %L%
TINYASM -f bin TTINY.ASM -o TTINY.COM >> %L%
IF ERRORLEVEL 1 ECHO TINYASM: errorlevel set >> %L%
TTINY >> %L%

ECHO === WASM
ECHO === WASM >> %L%
ECHO.|TIME >> %L%
WASM TWASM >> %L%
TWASM >> %L%

ECHO === GWBASIC
ECHO === GWBASIC >> %L%
ECHO.|TIME >> %L%
GWBASIC TGW.BAS > GW.TXT
ECHO (redirected output follows) >> %L%
TYPE GW.TXT >> %L%

ECHO === LDEBUG
ECHO === LDEBUG >> %L%
ECHO.|TIME >> %L%
LDEBUG TTINY.COM < LDSCRIPT.TXT >> %L%

ECHO === TURBO C
ECHO === TURBO C >> %L%
ECHO.|TIME >> %L%
IF NOT EXIST C:\TC\TCC.EXE ECHO no Turbo C on this image >> %L%
IF EXIST C:\TC\TCC.EXE TCC HTC.C >> %L%
ECHO.|TIME >> %L%
IF EXIST HTC.EXE HTC >> %L%

ECHO === BCC
ECHO === BCC >> %L%
ECHO.|TIME >> %L%
REM BCC only compiles names with a lowercase .c suffix
C:\DEVEL\BCC\BIN\BCC -o hbc.com hbc.c >> %L%
ECHO.|TIME >> %L%
IF EXIST HBC.COM HBC >> %L%

REM Pacific C 7.51 hangs on an 8088 right after its banner (its passes need a 286+), so it is not here.

ECHO.|TIME >> %L%
ECHO DEVTEST-DONE >> %L%
DIR /W >> %L%
ECHO DEVTEST-DONE
