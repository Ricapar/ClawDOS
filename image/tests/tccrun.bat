@ECHO OFF
REM Turbo C through CLAUDE's run tool on the 8088 (start a mock bridge on port
REM 18023, then python scripts/xttest.py image/tests/tccrun.bat): PRIMES.C ran
REM out of memory this way on the real XT when run went through a second
REM COMMAND.COM; started directly, TCC (and the TLINK it starts) must fit.
SET L=C:\DEVTEST.LOG
ECHO Turbo C through CLAUDE > %L%
CALL C:\NET\NETUP.BAT
C:
CD \TESTS\DEV
C:\CLAUDEXT\CLAUDE.EXE 10.0.2.2:18023 -unsafe -q -m "!run TCC PRIMES.C"
ECHO CLAUDE exit code %ERRORLEVEL% >> %L%
IF EXIST PRIMES.EXE PRIMES >> %L%
ECHO DEVTEST-DONE >> %L%
