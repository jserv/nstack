# nstack

## Overview

nstack is a Linux userspace TCP/IP stack. It was constructed to meet the following goals:
* Learn TCP/IP
* Learn Linux systems/network programming
* Learn Linux Socket API


## Build and Test

```shell
make
```

Set up test environment:
```shell
tools/testenv.sh start
tools/run.sh veth1
```

Execute `ping` inside test environment:
```shell
tools/ping_test.sh
```

Expected nstack messages:
```
arp_gratuitous: Announce 10.0.0.2
nstack_ingress_thread: Waiting for rx
nstack_ingress_thread: Frame received!
ether_input: proto id: 0x800
ip_input: proto id: 0x1
icmp_input: ICMP type: 8
nstack_ingress_thread: tick
nstack_ingress_thread: Waiting for rx
nstack_ingress_thread: Frame received!
ether_input: proto id: 0x800
ip_input: proto id: 0x1
icmp_input: ICMP type: 8
```

# Reference

* Level-IP, [source code](https://github.com/saminiir/level-ip) and [informative blog](http://www.saminiir.com/)
* Linux kernel TCP/IP stack, [source code](https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/net/ipv4)
* picoTCP, [source code](https://github.com/tass-belgium/picotcp)
* Xiaochen Wang's TCP/IP stack, [source code](https://github.com/chobits/tapip)
