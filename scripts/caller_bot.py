"""Minimal socket bot for engine.py: always answers 'C'. The engine coerces it
to check when calling is illegal and to a 0 bid during the auction, giving a
calling station that never bids."""
import socket
import sys

port = int(sys.argv[-1])
with socket.create_connection(("localhost", port)) as s:
    f = s.makefile("rw")
    for line in f:
        clauses = line.split()
        if "Q" in clauses:
            break
        f.write(("K" if any(c.startswith("D") for c in clauses) else "C") + "\n")
        f.flush()
