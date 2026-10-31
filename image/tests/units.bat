@ECHO OFF
REM Unit tests (scripts/boottest.py image/tests/units.bat --expect UNITS-OK).
C:\TESTS\DOSUNIT.EXE > COM1
ECHO CLAWDOS-TEST-DONE > COM1
