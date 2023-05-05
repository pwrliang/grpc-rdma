#!/usr/bin/env python3

import sys

n_client = int(sys.argv[2])
with open(sys.argv[1], 'r') as fi:
    server = None
    host_slots = []
    for line in fi:
        line = line.strip()
        if len(line) == 0:
            continue
        if server is None:
            server = line
        else:
            host_slots.append({"host": line, "slots": 0})
    if len(host_slots) == 0:
        print("At least two lines")
        exit(1)

    idx = 0
    while n_client > 0:
        host_slots[idx]["slots"] += 1
        idx += 1
        idx %= len(host_slots)
        n_client -= 1
    print(server)
    for kv in host_slots:
        if kv["slots"] > 0:
            print(kv["host"] + " slots=" + str(kv["slots"]))
