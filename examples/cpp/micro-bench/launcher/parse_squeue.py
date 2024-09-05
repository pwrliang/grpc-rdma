#!/usr/bin/env python3
import re
import sys

nodelist = sys.argv[1]
output = []

def process_a_group(group):
    m = re.match(r"^(.*?)\[(.*?)]$", group)
    if m is not None:
        prefix = m.groups()[0]
        node_ranges = m.groups()[1]
        for s_range in node_ranges.split(','):
            if '-' in s_range:
                begin_end = s_range.split('-')
                node_len = len(begin_end[0])
                begin = int(begin_end[0])
                end = int(begin_end[1])

                for node_id in range(begin, end + 1):
                    output.append(prefix + str(node_id).zfill(node_len))

            else:
                output.append(prefix + s_range)
    else:
        output.append(group)

if ',' in nodelist:
    for group in nodelist.split(","):
        process_a_group(group)
else:
    process_a_group(nodelist)

print(" ".join(output))