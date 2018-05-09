#!/bin/bash -e

LOCAL_IP=10.0.0.1
STACK_IP=10.0.0.2

function start {
    ip netns add TEST
    ip link add veth0 type veth peer name veth1
    ip link set dev veth0 up
    ip link set dev veth1 up
    ip addr add dev veth0 local $LOCAL_IP
    ip route add $STACK_IP dev veth0
    ip link set dev veth1 netns TEST
    ip netns exec TEST ip link set dev veth1 up
}

function stop {
    ip link set dev veth0 down
    ip link delete veth0
    ip netns delete TEST
}

function config {
    echo "$LOCAL_IP"
}

if [ $# -eq 0 ]; then
    echo "Usage: $(basename $0) {start|config|stop}"
    exit
fi
$1
