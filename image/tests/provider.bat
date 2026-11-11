@ECHO OFF
REM /model to another provider section mid-conversation (scripts/boottest.py
REM image/tests/provider.bat --mock --mock2, the expectations in the Makefile).
REM A chat with a tool call is made under the shipped [anthropic] section
REM (progress, dropthinking and threads on, a five-minute cache) against the
REM mock on 18023. It is reopened, and /model moves it to [plain], a host with
REM none of the betas and no cache, served by a second mock on 18024: the
REM conversation's fixed settings are chosen again from [plain], its prefix
REM keeps the prompt and tools but loses its cache marker (the mocks' request
REM lines show the system block's marker), and a turn with a tool round runs
REM there under the second mock's validator, the whole history sent without a
REM thread.
ECHO CLAWDOS-PROVIDER-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
ECHO [plain] > C:\EXTRA.TXT
ECHO type anthropic >> C:\EXTRA.TXT
ECHO tunnel 10.0.2.2:18024 >> C:\EXTRA.TXT
ECHO host api.anthropic.com >> C:\EXTRA.TXT
ECHO model claude-opus-5-5 >> C:\EXTRA.TXT
ECHO cache 0 >> C:\EXTRA.TXT
COPY C:\CLAWDOS\CLAWDOS.CFG+C:\EXTRA.TXT C:\NEW.CFG > NUL
COPY C:\NEW.CFG C:\CLAWDOS\CLAWDOS.CFG > NUL
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!tool sysinfo"
ECHO HARNESS.EXE first exit code %ERRORLEVEL% > COM1
REN C:\CLAWDOS\CHATS\*.JSN FIRST.JSN
REN C:\CLAWDOS\CHATS\*.PRE FIRST.PRE
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -c FIRST.JSN -unsafe -q -m "/model plain" -i "!tool sysinfo" -k "~e"
ECHO HARNESS.EXE switched exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
