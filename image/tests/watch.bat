@ECHO OFF
REM CLAUDE -watch (scripts/boottest.py image/tests/watch.bat --bridge --expect
REM "VRAM watch #1"): the mock pokes "XT" into B800:0000 behind the screen
REM code's back, as a foreign writer would; the watch must report it.
ECHO CLAUDEXT-WATCH-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAUDEXT\CLAUDE.EXE 10.0.2.2:18023 -watch -unsafe -q -m "!tool poke addr=B800:0000 bytes=58 4F 54 4F"
ECHO CLAUDE.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAUDEXT-TEST-DONE > COM1
