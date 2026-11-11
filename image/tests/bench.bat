@ECHO OFF
REM The benchmark (python3 scripts/bench.py, or make bench): in 86Box through
REM scripts/test8088.py --mock, so the mock Messages API runs on the host. The
REM mock's !bench makes 20 reads of C:\TESTS\BENCH\BENCH.TXT (4 KB each), a run
REM with full_memory (through CLAWDOS.EXE: a real relaunch) and a 4 KB reply.
REM CLAWDOS.LOG, with a line per round, goes to C:\BENCH.LOG for bench.py.
SET L=C:\BENCH.LOG
ECHO ClawDOS benchmark > %L%
CALL C:\NET\NETUP.BAT NOCLOCK
C:\CLAWDOS\CLAWDOS.EXE 10.0.2.2:18023 -unsafe -q -m !bench
ECHO CLAWDOS.EXE exit code %ERRORLEVEL% >> %L%
TYPE C:\CLAWDOS\CLAWDOS.LOG >> %L%
ECHO BENCH-DONE >> %L%
