@ECHO OFF
REM Awkward chunking (scripts/boottest.py image/tests/chunks.bat --mock --expect
REM "stop=end_turn"): the mock's !chunks cuts the HTTP chunks every 7 bytes, so
REM sizes, CRLFs, event lines and UTF-8 sequences straddle them.
ECHO CLAWDOS-CHUNKS-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!chunks"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
