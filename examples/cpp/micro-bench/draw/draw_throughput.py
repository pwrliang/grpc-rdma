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
            "phoenix/rpc_bench_rate/rpc_bench_rate_32b_{nc}c".format(nc=nc))
        rates.append(rate)
    rates.append(0)
    return np.asarray(rates)


def rr_vs_mrpc():
    def rpc_throughput(mode, concurrent, streaming, n_clients):
        tp_list = []
        dir = 'tput-scalability'
        for n in n_clients:
            path = "{dir}/client_tput_{mode}_cli_{n_clients}_concurrent_{concurrent}_streaming_{streaming}.log".format(
                dir=dir, mode=mode, n_clients=n, concurrent=concurrent, streaming=streaming)
            with open(path, 'r') as fi:
                for line in fi:
                    match = re.search(r"RPC Rate (.*?) RPCs/s", line)
                    if match:
                        tp_list.append(float(match.groups()[0]))
        return np.asarray(tp_list)

    n_clients = (1, 2, 4, 8, 16, 32, 64, 128)  #

    fig, axs = plt.subplots(nrows=1, ncols=2, figsize=(7.5, 3.5))
    x = np.arange(len(n_clients))  # the label locations
    streaming = "true"
    concurrent = "1"
    bpev_tp = rpc_throughput('RDMA_BPEV', concurrent, streaming, n_clients)

    phoenix_tp = read_phoenix()

    ax1 = axs[0]
    colors = ["gold", "red", "springgreen", "deepskyblue"]
    patterns = ['xxx', '', '', '']

    dataset = []
    dataset.append((phoenix_tp, 'mRPC'))
    dataset.append((bpev_tp, 'RR-Comp'))

    print("Phoenix", phoenix_tp / phoenix_tp[0])
    print("RR-Comp (BPEV)", bpev_tp)
    print("RR-Comp (speedup over 1 cli) ", bpev_tp / bpev_tp[0])
    print("RR-Comp (speedup over mRPC)", bpev_tp / phoenix_tp)
    idx = 1
    width = 0.35  # the width of the bars
    for data, label in dataset:
        if data is not None and len(data) > 0:
            data = data / 1000 / 1000  # kRPCs
            if idx == 1:
                ax1.bar((x + idx * width), data, width, label=label,
                        hatch=patterns[idx - 1], facecolor='none', edgecolor=colors[idx - 1],
                        lw=1, zorder=0)
                ax1.bar((x + idx * width), data, width,
                        edgecolor='black', facecolor='none', lw=1, zorder=1)
            else:
                ax1.bar((x + idx * width), data, width, label=label,
                        edgecolor='black', facecolor=colors[idx - 1], hatch=patterns[idx - 1])
            idx += 1

    ax1.text(-0.32, 0.2, 'Failed to run', rotation='vertical', va='center', transform=plt.gca().transAxes)
    ax1.set_xticks(x + width / 2 * idx, n_clients)

    ax1.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax1.set(xlabel='Number of Clients', ylabel='Throughput (MRPCs/s)')
    # ax1.xaxis.labelpad = 10
    # ax1.set_ylim((0, 10000))
    ax1.margins(x=0.02, y=0.4)
    ax1.set_title("(a) Aggregated Throughput")

    # Speedup
    x_rr = np.arange(len(n_clients[:-1]))  # the label locations
    x_mrpc = np.arange(len(n_clients[:-2]))  # the label locations

    ax2 = axs[1]

    speedup_rr = bpev_tp[1:] / bpev_tp[0]
    speedup_mrpc = phoenix_tp[1:-1] / phoenix_tp[0]
    ax2.plot(x_mrpc, speedup_mrpc, label="mRPC", color=colors[0], marker='o', linestyle='dotted')
    ax2.plot(x_rr, speedup_rr, label="RR-Comp", color=colors[1], marker='*')

    ax2.set_xticks(x_rr, n_clients[1:])

    ax2.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax2.set(xlabel='Number of Clients', ylabel='Speedup')
    # ax1.xaxis.labelpad = 10
    ax2.set_ylim((0,35))
    ax2.margins(x=0.05,)
    ax2.set_title("(b) Speedup over 1 Client")

    fig.tight_layout()
    fig.savefig('scalability_rr_vs_mrpc.pdf', format='pdf', bbox_inches='tight')
    # plt.show()


def tput_polling_thread():
    def rpc_throughput(n_threads):
        tp_list = []
        dir = 'tput-varying-polling-threads'
        for n in n_threads:
            path = "{dir}/client_tput_polling_thread_{polling_thread}_clients_64.log".format(
                dir=dir, polling_thread=n)
            with open(path, 'r') as fi:
                for line in fi:
                    match = re.search(r"RPC Rate (.*?) RPCs/s", line)
                    if match:
                        tp_list.append(float(match.groups()[0]))
        return np.asarray(tp_list)

    n_threads = (1, 2, 3, 4, 5, 6, 7, 8)  #

    fig, axs = plt.subplots(nrows=1, ncols=1, figsize=(4.5, 3.5))
    x = np.arange(len(n_threads))  # the label locations
    numa = "false"
    bpev_tp = rpc_throughput(n_threads)

    ax1 = axs
    colors = ["gold", "red", "springgreen", "deepskyblue"]
    patterns = ['xxx', '', '', '']

    dataset = []
    dataset.append((bpev_tp, 'RR-Comp (BPEV)'))
    width = 0.35  # the width of the bars
    for data, label in dataset:
        if data is not None and len(data) > 0:
            data = data / 1000  # kRPCs
            ax1.bar(x, data, width, label=label,
                    edgecolor='black', facecolor=colors[1], hatch=patterns[1])

    ax1.set_xticks(x, n_threads)

    ax1.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax1.set(xlabel='Number of Polling Threads', ylabel='Throughput (kRPCs/s)')
    # ax1.xaxis.labelpad = 10
    ax1.set_ylim((0, 6000))
    ax1.margins(x=0.1, )

    # fig.tight_layout()
    fig.savefig('tput_polling_thread.pdf'.format(numa=numa), format='pdf', bbox_inches='tight')
    # plt.show()


# tput_polling_thread()
rr_vs_mrpc()
