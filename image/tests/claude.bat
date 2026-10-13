@ECHO OFF
REM Visual test for 86Box: bring up the network and start a chat hands-free.
REM With the mock bridge (xtbridge.py --mock), !scroll returns 60 lines.
CALL C:\NET\NETUP.BAT
C:\CLAUDEXT\CLAUDE.EXE -m "!scroll"
