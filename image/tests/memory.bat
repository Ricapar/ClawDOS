@ECHO OFF
REM Memory tools never ask (scripts/boottest.py image/tests/memory.bat --bridge
REM --expect "memory_write -> ok"): safe mode is on (no -unsafe), so a y/n
REM prompt would wait for a key that never comes and the test would time out.
ECHO CLAWDOS-MEMORY-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -q -m "!tool memory_write name=SAFE text=Written_in_safe_mode"
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% > COM1
ECHO CLAWDOS-TEST-DONE > COM1
