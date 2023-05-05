mpirun --bind-to none -mca btl_tcp_if_include ib0 -hostfile testhosts ./cmake-build-debug-mri/mb \
  -mode bpev \
  -dir bi \
  -batch 200000
