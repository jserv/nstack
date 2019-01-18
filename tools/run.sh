#!/bin/bash -e

# shmem for sockets
dd if=/dev/zero of=/tmp/unetcat.sock bs=1024 count=1024
dd if=/dev/zero of=/tmp/tnetcat.sock bs=1024 count=1024

sudo setcap cap_net_raw,cap_net_admin,cap_net_bind_service+eip build/inetd
sudo ip netns exec TEST su $USER -c "build/inetd $1"
