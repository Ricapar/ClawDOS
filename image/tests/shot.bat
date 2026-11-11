@ECHO OFF
REM Screenshots (scripts/boottest.py image/tests/shot.bat --mock --timeout 240,
REM the Makefile lists the expectations): the run tool's picture of the screen.
REM C:\TESTS\QUADS.COM draws four quadrants and a diagonal in mode 4 and exits
REM leaving them; QUADT.COM draws the same, waits 3 s and sets text mode again,
REM so only shot_at=1.5 can see it. Each is taken in the harness and through the
REM launcher (full_memory), where CLAWDOS.EXE keeps the screen in SHOT.RAW and
REM the resumed HARNESS makes the PNG. The mock checks each PNG it gets
REM (signature, chunk CRCs, IHDR, zlib and Adler-32, rows), saves it as
REM build/mockshot-N.png and logs "image ok 320x200 (the call): palette ...,
REM pixels CRC", the digest tunnel/test_mockapi.py computes for QUADS. The first
REM image in a thread ends it (thread_not_found), so each is also a replay.
REM A text screen comes back as text (80 and 40 columns), a shot_at the run
REM never reaches says so, shot_mode reads the same memory as 640x200,
REM shot_color redraws it in palette 1, QUAD6.COM is a real mode 6 screen, bad
REM shot_mode and shot_at values are refused, and !shotmem checks that a
REM launcher screenshot leaves the harness no smaller (MEM before and after).
ECHO CLAWDOS-SHOT-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADS.COM screenshot=true"
ECHO exit exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADT.COM shot_at=1.5"
ECHO timed exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADS.COM shot_mode=640x200"
ECHO mode exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=VER screenshot=true"
ECHO text exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=VER shot_at=5"
ECHO late exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUAD6.COM screenshot=true"
ECHO mode 6 exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\TEXT40.COM screenshot=true"
ECHO text40 exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADS.COM shot_color=48"
ECHO color exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=VER shot_mode=1024x768"
ECHO bad mode exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=VER shot_at=0.05"
ECHO bad time exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADS.COM screenshot=true full_memory=true"
ECHO launcher exit exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool run command=C:\TESTS\QUADT.COM shot_at=1.5 full_memory=true"
ECHO launcher timed exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!shotmem"
ECHO shotmem exit code %ERRORLEVEL% > COM1
IF NOT EXIST C:\CLAWDOS\SHOT.RAW ECHO SHOT.RAW consumed > COM1
IF NOT EXIST C:\CLAWDOS\SHOT.PNG ECHO SHOT.PNG consumed > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
