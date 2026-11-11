@ECHO OFF
REM Memory tools never ask (scripts/boottest.py image/tests/memory.bat --mock
REM --expect "memory_write -> ok"): safe mode is on (no -unsafe), so a y/n
REM prompt would wait for a key that never comes and the test would time out.
ECHO CLAWDOS-MEMORY-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!tool memory_write name=SAFE text=Written_in_safe_mode"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
