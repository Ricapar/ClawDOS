@ECHO OFF
REM Load a packet driver at INT 60h, then get an address via DHCP.
REM PM2000 auto-detects a PicoMEM; otherwise fall back to a plain NE2000
REM at I/O 300h, IRQ 3 (emulators).
C:\PICOMEM\PM2000.COM 0x60
IF NOT ERRORLEVEL 1 GOTO DHCP
C:\PICOMEM\NE2000.COM 0x60 0x3 0x300
IF ERRORLEVEL 1 GOTO FAIL
:DHCP
C:\NET\DHCP.EXE
GOTO END
:FAIL
ECHO No network card found.
:END
