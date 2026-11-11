@ECHO OFF
REM The tool tour's stdin probe with COMMAND.COM as its parent (tools.bat): the NUL
REM standard input the run tool gives reaches a program a batch file starts.
C:\TESTS\STDINEOF.COM
IF ERRORLEVEL 1 GOTO BAD
ECHO STDINEOF under COMMAND.COM: ok
GOTO END
:BAD
ECHO STDINEOF under COMMAND.COM: failed
:END
