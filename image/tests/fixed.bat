@ECHO OFF
REM A conversation keeps the thinking and cache settings it started with
REM (scripts/boottest.py image/tests/fixed.bat --mock --timeout 150 --expect
REM "tool run -> ok" --expect "1 messages, last role user, thinking none, cache 1h"
REM --reject "VALIDATION:"). The first conversation starts under the shipped
REM settings (progress 1, dropthinking 1, five-minute cache); its full_memory
REM run copies in a CLAWDOS.CFG that turns both off and asks for cachettl 1h,
REM and the relaunched harness must still send what the conversation began
REM with (RESUME.TXT carries it): the mock rejects (VALIDATION:) a thinking
REM object or a top-level cache marker that changed within a conversation, and
REM markers whose ttl disagree. A new conversation then takes the new settings.
ECHO CLAWDOS-FIXED-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
ECHO progress 0 > C:\EXTRA.TXT
ECHO dropthinking 0 >> C:\EXTRA.TXT
ECHO cachettl 1h >> C:\EXTRA.TXT
COPY C:\CLAWDOS\CLAWDOS.CFG+C:\EXTRA.TXT C:\NEW.CFG > NUL
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!bigrun COPY C:\NEW.CFG C:\CLAWDOS\CLAWDOS.CFG"
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "hello"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
