#!/usr/bin/env bash



hosts=(p0037 p0046 p0048 p0050 p0072 p0101 p0110 p0112 p0128)
rm -f hosts

for host in ${hosts[*]}; do
  ip=$(ssh -q $host "/usr/sbin/ip -f inet addr show ib0 | sed -En -e 's/.*inet ([0-9.]+).*/\1/p'")
  echo "$ip" >> hosts
done