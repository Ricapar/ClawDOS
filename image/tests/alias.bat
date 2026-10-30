@ECHO OFF
CALL C:\NET\NETUP.BAT
ECHO CLAWDOS-ALIAS-TEST > COM1
REM The old name must still start CLAWDOS.EXE, quoted arguments included.
CLAUDE 10.0.2.2:18023 -unsafe -q -m "!tool sysinfo"
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAWDOS-TEST-DONE > COM1
