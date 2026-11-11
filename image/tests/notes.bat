@ECHO OFF
REM How a progress note is drawn (scripts/boottest.py image/tests/notes.bat --mock
REM --expect "progress note: Two lines"): the mock's !note sends text, a call, then
REM a note with blank lines and spaces before it, a line break inside and blank
REM lines after, then more text. CLAWDOS.LOG shows the note as drawn: the blanks
REM before it trimmed and the break joined (a space in the log line). Then
REM !headercol: reply text, a tool call, and peeks of CGA memory until the mock
REM finds the tool header's row: its bullet must be in column 0, not in the
REM reply's margin (--expect "tool header column: 0 (PASS)").
ECHO CLAWDOS-NOTES-TEST > COM1
CALL C:\NET\NETUP.BAT
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!note"
ECHO HARNESS.EXE exit code %ERRORLEVEL% > COM1
C:\CLAWDOS\HARNESS.EXE 10.0.2.2:18023 -unsafe -q -m "!headercol"
ECHO HARNESS.EXE headercol exit code %ERRORLEVEL% > COM1
TYPE C:\CLAWDOS\CLAWDOS.LOG > COM1
ECHO CLAWDOS-TEST-DONE > COM1
