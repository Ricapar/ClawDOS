@ECHO OFF
REM Visual test for 86Box: bring up the network and start a chat hands-free.
REM With the mock API (tunnel/mockapi.py), !scroll returns 60 lines.
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\CLAWDOS.EXE -m "!scroll"
