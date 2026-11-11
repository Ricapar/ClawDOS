@ECHO OFF
REM Skills (scripts/boottest.py image/tests/skills.bat --mock --expect "skills: PASS"):
REM the mock's !skills checks that the system prompt lists C:\SKILLS\DEVTOOLS.TXT
REM and C:\SKILLS\DEMOFX.TXT under the skills and neither under the memories,
REM then reads DEVTOOLS.TXT 40 lines at a time: the first result must end by
REM saying where to go on, and the second must reach the file's last line.
REM Safe mode stays on: read never asks.
ECHO CLAWDOS-SKILLS-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "!skills"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
