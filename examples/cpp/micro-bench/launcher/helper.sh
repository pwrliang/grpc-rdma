#!/usr/bin/env bash



hosts=$(./parse_squeue.py "$1")
rm -f hosts

for host in ${hosts[*]}; do
  ip=$(ssh -q $host "/usr/sbin/ip -f inet addr show ib0 | sed -En -e 's/.*inet ([0-9.]+).*/\1/p'")
  echo "$ip" >> hosts
done