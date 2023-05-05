mpirun --bind-to none -mca btl_tcp_if_include ib0 -hostfile testhosts ./build/mb \
  -mode bpev \
  -dir bi \
  -batch 200000
