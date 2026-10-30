@ECHO OFF
REM Load a packet driver at INT 60h, then get an address via DHCP.
REM Real XT: PM2000 (PicoMEM's NE2000-over-WiFi, auto-detected).
REM Emulators: Crynwr NE2000 (DOSBox-X) or NE1000 (86Box) at I/O 340h, IRQ 3.
REM Both cards use the same 8390 chip, so each Crynwr driver also "loads" on the
REM other card and then silently drops every packet (different buffer size and
REM bus width). A quick DHCP probe tells them apart: no answer means wrong driver.
C:\PICOMEM\PM2000.COM 0x60
IF NOT ERRORLEVEL 1 GOTO DHCP
C:\NET\NE2000.COM 0x60 3 0x340
IF ERRORLEVEL 1 GOTO NE1000
C:\NET\DHCP.EXE -timeout 5 -retries 1
IF NOT ERRORLEVEL 1 GOTO SYNC
C:\NET\NE2000.COM -u 0x60
:NE1000
C:\NET\NE1000.COM 0x60 3 0x340
IF ERRORLEVEL 1 GOTO FAIL
:DHCP
C:\NET\DHCP.EXE
:SYNC
REM Sync the clock while the network stack is still free (before CLAWDOS.EXE).
SET TZ=EST5EDT
C:\NET\SNTP.EXE -set time.nist.gov
GOTO END
:FAIL
ECHO No network card found.
:END
