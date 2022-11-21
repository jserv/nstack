#!/usr/bin/env bash

make
sudo ./tools/testenv.sh start
sleep 1s
./tools/run.sh veth1 > /dev/null 2>&1 &
sleep 1s
./build/tnetcat > /tmp/log 2>&1 &
sleep 1s
expect="foo"
echo -n ${expect}|nc  10.0.0.2 10 > /dev/null 2>&1 &
sleep 1s
result=$(cat /tmp/log)
if [ ${expect} = ${result} ]; then
    echo "expect: ${expect} equl to result: ${result}"
else
    echo "expect: ${expect} not equl to result: ${result}"
    exit 1
fi