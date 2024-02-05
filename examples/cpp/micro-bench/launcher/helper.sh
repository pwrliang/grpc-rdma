#!/usr/bin/env bash


rm -f IPs.txt

hosts=(p0038 p0039 p0098 p0099 p0109 p0126 p0127 p0129 p0130)

for host in ${hosts[*]}; do
  ip=$(ssh -q $host "/usr/sbin/ip -f inet addr show ib0 | sed -En -e 's/.*inet ([0-9.]+).*/\1/p'")
  echo "$ip" >> IPs.txt
done

