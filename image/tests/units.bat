@ECHO OFF
REM Unit tests (scripts/boottest.py image/tests/units.bat --expect UNITS-OK).
C:\TESTS\CXTUNIT.EXE > COM1
ECHO CLAUDEXT-TEST-DONE > COM1
