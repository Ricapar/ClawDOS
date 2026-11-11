@ECHO OFF
REM Message threads when they go wrong (scripts/boottest.py image/tests/threads.bat
REM --mock --timeout 300, the expectations in the Makefile): each run makes a tool
REM call, and the request after it is a continue that the mock refuses or cuts.
REM !threadlost: the mock forgets the thread (thread_not_found), so CLAWDOS replays
REM the whole conversation as a new thread; the mock logs whether the replay was
REM the whole conversation. !threaddrop: the link drops in the continue's reply and
REM the same continue goes again; with burn the resend meets
REM thread_already_continued and CLAWDOS replays. !nothreads: the continue meets
REM thread_unsupported_request, and the rest of the conversation goes without a
REM thread. !threadfail: the continue and its replay are both refused, which ends
REM the turn with the message. !threadlost run: a full_memory run through
REM CLAWDOS.EXE comes back to a thread that has gone (as one past its five
REM minutes would), and the resumed harness replays. !threadretry: the continue
REM meets a 529, or an overloaded_error event, once and is sent again as the
REM same continue; a plain 404 or 400 (no thread code) ends the turn.
ECHO CLAWDOS-THREADS-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!threadlost"
ECHO HARNESS.EXE threadlost exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!threaddrop"
ECHO HARNESS.EXE threaddrop exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!threaddrop burn"
ECHO HARNESS.EXE threaddrop burn exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!nothreads"
ECHO HARNESS.EXE nothreads exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!threadfail"
ECHO HARNESS.EXE threadfail exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!threadretry 529"
ECHO HARNESS.EXE threadretry 529 exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!threadretry overload"
ECHO HARNESS.EXE threadretry overload exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!threadretry 404"
ECHO HARNESS.EXE threadretry 404 exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!threadretry 400"
ECHO HARNESS.EXE threadretry 400 exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m "!threadlost run"
ECHO CLAWDOS.EXE threadlost run exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
