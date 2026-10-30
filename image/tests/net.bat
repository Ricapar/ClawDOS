@ECHO OFF
REM Network test for DOSBox-X (scripts/boottest.py): NETUP (packet driver + DHCP),
REM then ping the host. NETUP's own output stays on screen; the ping replies on COM1
REM prove that the right driver was loaded and a lease was obtained.
ECHO CLAWDOS-NET-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\NET\PING.EXE -count 3 10.0.2.2 > COM1
ECHO CLAWDOS-TEST-DONE > COM1
