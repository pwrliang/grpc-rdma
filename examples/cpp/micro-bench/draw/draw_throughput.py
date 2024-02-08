import matplotlib.pyplot as plt
import re
import numpy as np
import os

import comm_settings


def read_rates(path):
    rates = {}
    with open(path, 'r') as fi:
        for line in fi:
            line = line.strip()
            # print(line)
            m = re.search(r'Thread (\d+), (.*?) rps', line)
            if m is not None:
                thread_id = int(m.groups()[0])
                rates.setdefault(thread_id, []).append(float(m.groups()[1]))
    return rates


def summarize_rates(dir):
    stdouts = [os.path.join(dir, f) for f in os.listdir(dir) if f.endswith("stdout")]
    tp = 0
    for stdout in stdouts:
        for tid, rates in read_rates(stdout).items():
            tp += rates[4]
            # print("TID", tid, "Rates", rates)
    # print("Total Rates", tp)
    return tp


def read_phoenix():
    clients = ["1", "2", "4", "8", "16", "32", "64"]
    rates = []

    for nc in clients:
        rate = summarize_rates(
            "/Users/liang/mrpc-eval-32-8/benchmark/rpc_bench_rate/rpc_bench_rate_32b_{nc}c".format(nc=nc))
        rates.append(rate)
    return np.asarray(rates)


def rpc_throughput(mode, concurrent, streaming, n_clients, numa):
    tp_list = []
    dir = 'th-pitzer-40-threads'
    for n in n_clients:
        path = "{dir}/client_{mode}_tput_cli_{n_clients}_numa_{numa}_concurrent_{concurrent}_streaming_{streaming}.log".format(
            dir=dir, mode=mode, n_clients=n, numa=numa, concurrent=concurrent, streaming=streaming)
        with open(path, 'r') as fi:
            for line in fi:
                match = re.search(r"RPC Rate (.*?) RPCs/s", line)
                if match:
                    tp_list.append(float(match.groups()[0]))
    return np.asarray(tp_list)


n_clients = (1, 2, 4, 8, 16, 32, 64,)  #

fig, axs = plt.subplots(nrows=1, ncols=1, figsize=(5, 4))
x = np.arange(len(n_clients))  # the label locations
numa = "false"
streaming = "true"
concurrent = "1"
# tcp_tp = rpc_throughput('TCP', concurrent, streaming, n_clients, numa)
# event_tp = rpc_throughput('RDMA_EVENT', concurrent, streaming, n_clients, numa)
bp_tp = rpc_throughput('RDMA_BP', concurrent, streaming, n_clients, numa)
bpev_tp = rpc_throughput('RDMA_BPEV', concurrent, streaming, n_clients, numa)
# bp_tp_no_numa = rpc_throughput('RDMA_BP', concurrent, streaming, n_clients, "false")
# bpev_tp_no_numa = rpc_throughput('RDMA_BPEV', concurrent, streaming, n_clients, "false")


phoenix_tp = read_phoenix()

ax1 = axs
patterns = ['', '\\\\', '\\\\--', '..', '..--']
light_colors = ['#6C87EA', 'lightcoral', '#FF3333', 'lemonchiffon', '#FFDF33', 'powderblue', '#33FFFF', ]
dataset = []
# dataset.append((tcp_tp, 'gRPC-IPoIB'))
# dataset.append((event_tp, 'EVENT'))
dataset.append((bp_tp, 'BP'))
dataset.append((bpev_tp, 'BPEV'))

# dataset.append((bp_tp, 'RR-Compound'))
dataset.append((phoenix_tp, 'Phoenix'))
# dataset.append((bp_tp_numa_true, "BP-NUMA"))
# dataset.append((bp_tp_numa_false, "BP"))
idx = 1
width = 0.2  # the width of the bars
for data, label in dataset:
    if data is not None and len(data) > 0:
        data = data / 1000  # kRPCs
        bar = ax1.bar((x + idx * width), data, width, label=label, hatch=patterns[idx - 1],
                      facecolor=light_colors[idx - 1])
        idx += 1

ax1.set_xticks(x + width * (idx - 2), n_clients)

ax1.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
           frameon=False)
ax1.set(xlabel='Number of Clients', ylabel='Throughput (kRPCs/s)')
ax1.xaxis.labelpad = 10
# ax1.set_ylim((0, max_tp + max_tp * 0.3))
# ax1.margins(x=0.01, )

fig.tight_layout()
fig.savefig('throughput_numa_{numa}.pdf'.format(numa=numa), format='pdf', bbox_inches='tight')
plt.show()
