#!/usr/bin/env bash


rm -f IPs.txt

hosts=(p0148 p0149 p0150 p0151 p0171 p0172 p0173 p0174 p0175)

for host in ${hosts[*]}; do
  ip=$(ssh -q $host "/usr/sbin/ip -f inet addr show ib0 | sed -En -e 's/.*inet ([0-9.]+).*/\1/p'")
  echo "$ip" >> IPs.txt
done

