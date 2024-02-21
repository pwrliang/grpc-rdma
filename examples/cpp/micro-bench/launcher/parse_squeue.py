#!/usr/bin/env python3
import re
import sys

nodelist = sys.argv[1]
output = []

m = re.match(r"^(.*?)\[(.*?)]$", nodelist)
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
    output.append(nodelist)
print(" ".join(output))