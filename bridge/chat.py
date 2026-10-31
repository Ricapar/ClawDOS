#!/usr/bin/env python3
"""Minimal terminal client for DOS Bridge (for testing from a modern machine).

  python bridge/chat.py [host] [port] [--token T] [-m "one message"]
"""

import argparse
import socket
import sys


def lines(sock):
    """Yield each newline-terminated line received on `sock` (as bytes)."""
    buf = b''
    while True:
        while b'\n' not in buf:
            data = sock.recv(4096)
            if not data:
                return
            buf += data
        line, buf = buf.split(b'\n', 1)
        yield line


def converse(sock, rx, text):
    """Send one message and print the reply; False if the link closed first."""
    sock.sendall(b'MSG ' + text.encode('cp437', 'replace') + b'\n')
    for line in rx:
        verb, _, rest = line.partition(b' ')
        rest = rest.decode('cp437')
        if verb == b'TXT':
            sys.stdout.write(rest)
        elif verb == b'NL':
            sys.stdout.write('\n')
        elif verb == b'END':
            print(f'\n[end{" " + rest if rest else ""}]')
            return True
        elif verb == b'ERR':
            print(f'\n[error: {rest}]')
            return True
        elif verb == b'TOOL':        # this client runs no tools; say so
            tool_id, _, name = rest.partition(' ')
            print(f'[tool {name}]')
        elif verb == b'CALL':
            sock.sendall(b'OUT this client has no tools\nDONE ' + tool_id.encode() + b' error\n')
        sys.stdout.flush()
    return False


def main():
    """Connect, then chat interactively or send the -m messages."""
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ap = argparse.ArgumentParser()
    ap.add_argument('host', nargs='?', default='127.0.0.1')
    ap.add_argument('port', nargs='?', type=int, default=8023)
    ap.add_argument('--token')
    ap.add_argument('-m', '--message', action='append', help='send and exit (repeatable)')
    a = ap.parse_args()

    sock = socket.create_connection((a.host, a.port))
    rx = lines(sock)
    print(next(rx).decode('cp437'))
    if a.token:
        sock.sendall(b'AUTH ' + a.token.encode() + b'\n')
        print(next(rx).decode('cp437'))
    sock.sendall(b'INFO\n')
    print(next(rx).decode('cp437'))
    if a.message:
        for m in a.message:
            print(f'> {m}')
            converse(sock, rx, m)
    else:
        while True:
            try:
                text = input('> ')
            except EOFError:
                break
            if text.strip() == '/new':
                sock.sendall(b'NEW\n')
                print(next(rx).decode('cp437'))
            elif text.strip():
                if not converse(sock, rx, text):
                    break
    sock.sendall(b'QUIT\n')


if __name__ == '__main__':
    main()
