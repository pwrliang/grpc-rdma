#!/usr/bin/env python3

import sys

n_client = int(sys.argv[2])
with open(sys.argv[1], 'r') as fi:
    host_slots = []
    for line in fi:
        line = line.strip()
        if len(line) == 0:
            continue
        host_slots.append({"host": line, "slots": 0})
    if len(host_slots) < 2:
        print("At least two lines")
        exit(1)

    host_slots[0]["slots"] = 1
    idx = 1
    while n_client > 0:
        host_slots[idx]["slots"] += 1
        idx += 1
        if idx == len(host_slots):
            idx = 1
        n_client -= 1
    for kv in host_slots:
        if kv["slots"] > 0:
            print(kv["host"] + " slots=" + str(kv["slots"]))
