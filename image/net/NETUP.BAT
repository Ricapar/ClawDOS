@ECHO OFF
REM Load a packet driver at INT 60h, get an address via DHCP, then set the clock by NTP.
REM NETUP NOCLOCK (upper case) stops after DHCP: the boot tests use it, so they
REM reach no server but the mock on the host (image/tests/clock.bat sets the clock).
REM Real PC: PM2000 (PicoMEM's NE2000-over-WiFi, auto-detected).
REM Emulators: Crynwr NE2000 (DOSBox-X) or NE1000 (86Box) at I/O 340h, IRQ 3.
REM Both cards use the same 8390 chip, so each Crynwr driver also "loads" on the
REM other card and then silently drops every packet (different buffer size and
REM bus width). A quick DHCP probe tells them apart: no answer means wrong driver.
C:\PICOMEM\PM2000.COM 0x60
IF NOT ERRORLEVEL 1 GOTO DHCP
C:\NET\NE2000.COM 0x60 3 0x340
IF ERRORLEVEL 1 GOTO NE1000
C:\NET\DHCP.EXE -timeout 5 -retries 1
IF NOT ERRORLEVEL 1 GOTO CLOCK
C:\NET\NE2000.COM -u 0x60
:NE1000
C:\NET\NE1000.COM 0x60 3 0x340
IF ERRORLEVEL 1 GOTO FAIL
:DHCP
C:\NET\DHCP.EXE
IF ERRORLEVEL 1 GOTO END
:CLOCK
IF "%1"=="NOCLOCK" GOTO END
REM SNTP needs TZ (AUTOEXEC.BAT). NTPSERVER overrides the server; the fixed
REM address (time-a-g.nist.gov) is for when DNS fails, as it does in DOSBox-X on
REM a host whose first nameserver is IPv6. With ASTCLOCK /R resident, the time
REM SNTP sets also goes into the PicoMEM 2's clock.
IF "%NTPSERVER%"=="" SET NTPSERVER=pool.ntp.org
C:\NET\SNTP.EXE -set %NTPSERVER% > C:\NET\SNTP.LOG
IF NOT ERRORLEVEL 1 GOTO LEASE
C:\NET\SNTP.EXE -set 129.6.15.28 >> C:\NET\SNTP.LOG
IF NOT ERRORLEVEL 1 GOTO LEASE
ECHO Clock not set by NTP: see C:\NET\SNTP.LOG
GOTO END
:LEASE
ECHO Clock set by NTP.
REM DHCP stamped its lease with the old clock. After a jump (a PC that booted
REM in 1980) mTCP would call the lease expired and every network program would
REM refuse to run, so take a fresh lease at the new time.
FIND "greater than 10 minutes" C:\NET\SNTP.LOG > NUL
IF ERRORLEVEL 1 GOTO END
C:\NET\DHCP.EXE > NUL
IF ERRORLEVEL 1 ECHO DHCP failed after the clock moved: type NETUP to retry.
GOTO END
:FAIL
ECHO No network card found.
:END
