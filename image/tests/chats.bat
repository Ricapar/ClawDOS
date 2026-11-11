@ECHO OFF
REM The chat list and reopening (scripts/boottest.py image/tests/chats.bat --mock,
REM the expectations in the Makefile). A chat with a tool call is made and named
REM FIRST, then a plain second chat. -c FIRST.JSN reopens the first: the mock's
REM !tailscreen peeks CGA memory for its transcript (the typed line, the tool
REM header, the reply, in order) and the validator sees the whole history come
REM back as the mock sent it. Then -k types into the chat list (/chats, newest
REM first, whose rows go to CLAWDOS.LOG as video memory shows them): Down, Enter
REM opens the second chat, and !tailscreen checks its transcript. Last, Down,
REM Del and y delete FIRST and its prefix file; Esc and F10 leave.
ECHO CLAWDOS-CHATS-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool sysinfo"
ECHO HARNESS.EXE first exit code %ERRORLEVEL% > COM1
REN C:\CLAWDOS\CHATS\*.JSN FIRST.JSN
REN C:\CLAWDOS\CHATS\*.PRE FIRST.PRE
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -m "Hello again, a second chat"
ECHO HARNESS.EXE second exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c FIRST.JSN -unsafe -q -m "!tailscreen"
ECHO HARNESS.EXE reopen exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -k "~n~e!tailscreen~e" -m "/chats"
ECHO HARNESS.EXE list exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -q -k "~n~dy~x~q" -m "/chats"
ECHO HARNESS.EXE delete exit code %ERRORLEVEL% > COM1
IF EXIST C:\CLAWDOS\CHATS\FIRST.JSN ECHO FIRST.JSN is still there > COM1
IF EXIST C:\CLAWDOS\CHATS\FIRST.PRE ECHO FIRST.PRE is still there > COM1
DIR /B C:\CLAWDOS\CHATS > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
