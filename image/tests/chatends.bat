@ECHO OFF
REM Reopening chats that ended oddly (scripts/boottest.py image/tests/chatends.bat
REM --mock, the expectations in the Makefile). The files in C:\TESTS\CHATS are
REM what the PC leaves when it goes off mid-turn: UNANS a typed message nothing
REM answered, CALL a tool call whose result was never written, RESULT a stopped
REM call's result waiting for the next text, HALF a reply cut off mid-stream
REM (HARNESS cuts it at open, leaving the typed message before it unanswered),
REM and BIG a 206 KB chat of 18 exchanges, put together from pieces with COPY /B
REM (opening it must not need the whole file in memory; the transcript shows its
REM last three exchanges, each text cut at 1,000 bytes of its JSON). NOTCHAT is a
REM file that is no chat: refused, and left whole. -c takes no path. NOSUCH is not there.
REM Each is opened with -c and continued with the mock's !reopened, which logs
REM the messages that came: roles must still alternate, the next text joining
REM the message it follows, and the call answered with an error result.
ECHO CLAWDOS-CHATENDS-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
MD C:\CLAWDOS\CHATS
COPY C:\TESTS\CHATS\*.JSN C:\CLAWDOS\CHATS > NUL
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c UNANS -q -m "!reopened unans"
ECHO HARNESS.EXE unans exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c CALL -q -m "!reopened call"
ECHO HARNESS.EXE call exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c RESULT -q -m "!reopened result"
ECHO HARNESS.EXE result exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c HALF -q -m "!reopened half"
ECHO HARNESS.EXE half exit code %ERRORLEVEL% > COM1
CD C:\TESTS\CHATS
COPY /B BIGPAIR.PRT+BIGPAIR.PRT C:\P2.PRT > NUL
COPY /B C:\P2.PRT+C:\P2.PRT C:\P4.PRT > NUL
COPY /B C:\P4.PRT+C:\P4.PRT C:\P8.PRT > NUL
COPY /B C:\P8.PRT+C:\P8.PRT C:\P16.PRT > NUL
COPY /B BIGHEAD.PRT+C:\P16.PRT+BIGPAIR.PRT+BIGEND.PRT C:\CLAWDOS\CHATS\BIG.JSN > NUL
DEL C:\P*.PRT
CD \
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c BIG -q -m "!reopened big"
ECHO HARNESS.EXE big exit code %ERRORLEVEL% > COM1
ECHO A file that is no chat {kept} > C:\CLAWDOS\CHATS\NOTCHAT.JSN
ECHO and its second line stays >> C:\CLAWDOS\CHATS\NOTCHAT.JSN
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c NOTCHAT -q -m "!reopened notchat"
ECHO HARNESS.EXE notchat exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CHATS\NOTCHAT.JSN > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c C:\AUTOEXEC.BAT -q -m "!reopened path"
ECHO HARNESS.EXE path exit code %ERRORLEVEL% > COM1
REM an emptied file would still exist: its last line must still be in it
FIND "ClawDOS ready." C:\AUTOEXEC.BAT > NUL
IF NOT ERRORLEVEL 1 ECHO AUTOEXEC.BAT still whole > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c NOSUCH -q -m "!reopened nosuch"
ECHO HARNESS.EXE nosuch exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
