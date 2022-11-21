#!/usr/bin/env bash

make
sudo ./tools/testenv.sh start
sleep 1s
./tools/run.sh veth1 > /dev/null 2>&1 &
sleep 1s
nc -l 10.0.0.1 10000 > /tmp/log 2>&1 &
sleep 1s
./build/tcptest > /dev/null 2>&1 &
expect="foo"
sleep 10s
result=$(cat /tmp/log)
if [ ${expect} = ${result} ]; then
    echo "expect: ${expect} equl to result: ${result}"
else
    echo "expect: ${expect} not equl to result: ${result}"
    exit 1
fi