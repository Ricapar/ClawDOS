@ECHO OFF
REM Turbo C through CLAUDE's run tool on the 8088 (start a mock bridge on port
REM 18023, then python scripts/xttest.py image/tests/tccrun.bat): TPRIMES.C ran
REM out of memory this way on the real XT when run went through a second
REM COMMAND.COM; started directly, TCC (and the TLINK it starts) must fit.
SET L=C:\DEVTEST.LOG
ECHO Turbo C through CLAUDE > %L%
CALL C:\NET\NETUP.BAT
C:
CD \TESTS\DEV
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!run TCC TPRIMES.C"
ECHO CLAUDE exit code %ERRORLEVEL% >> %L%
IF EXIST TPRIMES.EXE TPRIMES >> %L%
ECHO DEVTEST-DONE >> %L%
