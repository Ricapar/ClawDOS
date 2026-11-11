@ECHO OFF
REM A refusal after a relaunch (scripts/boottest.py image/tests/runrefuse.bat --mock
REM --timeout 120 --expect "stop=refusal" --expect "relaunch:" --reject '"content":"!runrefuse"'
REM --reject toolu_mock):
REM the mock's !runrefuse makes a full_memory run, which CLAWDOS.EXE runs while the
REM harness is out of memory, and refuses on the round after it. The resumed harness
REM carries the turn's start over (RESUME.TXT), so the whole turn goes: the user's
REM text and the call with it, not only the result: the chat file, typed to COM1,
REM holds neither.
ECHO CLAWDOS-RUNREFUSE-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!runrefuse"
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% > COM1
FOR %%F IN (C:\CLAWDOS\CHATS\*.JSN) DO TYPE %%F > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
