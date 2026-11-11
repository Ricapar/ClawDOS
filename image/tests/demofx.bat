@ECHO OFF
REM CGA demo-effect checks on the 8088 (python scripts/test8088.py image/tests/demofx.bat):
REM assembles each C:\TESTS\DEMO\*.ASM with TINYASM, as Claude would, and runs it.
REM The effects themselves are judged from screenshots taken while they run.
SET L=C:\DEVTEST.LOG
C:
CD \TESTS\DEMO
ECHO CGA demo effects test > %L%
TINYASM -f bin RASTER.ASM -o RASTER.COM >> %L%
IF ERRORLEVEL 1 ECHO RASTER: TINYASM failed >> %L%
ECHO RASTER start >> %L%
IF EXIST RASTER.COM RASTER >> %L%
ECHO DEVTEST-DONE >> %L%
