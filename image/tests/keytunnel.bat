@ECHO OFF
REM The keyed tunnel end to end, offline (scripts/boottest.py
REM image/tests/keytunnel.bat --mock --key-tunnel, the expectations in the
REM Makefile). The image has no key (boot test images never do) and its
REM [anthropic] section says auth tunnel, so HARNESS.EXE sends no x-api-key;
REM it talks to tunnel.py's key mode on 10.0.2.2:18025, which frames each
REM request on the keep-alive connection, adds a made-up key and passes it to
REM the mock over plain TCP. The mock logs that a key of that length arrived
REM on both requests of a tool round (never the key itself), and the tunnel
REM counts the requests it rewrote.
ECHO CLAWDOS-KEYTUNNEL-TEST > COM1
CALL C:\NET\NETUP.BAT NOCLOCK
ECHO auth tunnel > C:\EXTRA.TXT
COPY C:\CLAWDOS\CLAWDOS.CFG+C:\EXTRA.TXT C:\NEW.CFG > NUL
COPY C:\NEW.CFG C:\CLAWDOS\CLAWDOS.CFG > NUL
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18025 -unsafe -q -m "!tool sysinfo"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
