@ECHO OFF
REM Screenshots on a true 8088 (python scripts/test8088.py image/tests/shot8088.bat
REM --mock --timeout 1500 --expect "pixels B5BCA4D2"): the mode-4 picture taken at
REM exit and at shot_at in the harness, and at shot_at through the launcher (the
REM timer hook and the 16 KB copy on the real CPU and CGA timing), then the unit
REM tests, which time the PNG encoder and the lending of the harness's buffers
REM (CLAWDOS.LOG's run: line times an in-harness run). The mock logs "image ok" for each PNG.
SET L=C:\DEVTEST.LOG
ECHO Screenshot test on the 8088 > %L%
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADS.COM screenshot=true"
ECHO exit exit code %ERRORLEVEL% >> %L%
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADT.COM shot_at=1.5"
ECHO timed exit code %ERRORLEVEL% >> %L%
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADT.COM shot_at=1.5 full_memory=true"
ECHO launcher timed exit code %ERRORLEVEL% >> %L%
C:\TESTS\DOSUNIT.EXE > C:\UNITS.TXT
FIND "PNG written in" C:\UNITS.TXT >> %L%
FIND "runmem:" C:\UNITS.TXT >> %L%
FIND "UNITS-" C:\UNITS.TXT >> %L%
TYPE C:\CLAWDOS\CLAWDOS.LOG >> %L%
ECHO DEVTEST-DONE >> %L%
