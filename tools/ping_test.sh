#!/bin/bash

source tools/assert.sh
source tools/testenv.sh config

ASSERT ping -c 3 -w 5 -s 500 $STACK_IP
ASSERT ping -c 3 -w 5 -s 1500 $STACK_IP
ASSERT ping -c 3 -w 5 -s 4500 $STACK_IP
