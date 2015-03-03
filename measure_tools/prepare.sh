#!/bin/bash
xl vcpu-pin Domain-0 0 0
xl vcpu-pin Domain-0 1 1
#xl vcpu-pin linux-guest-1 0 0
#xl vcpu-pin linux-guest-1 1 1
taskset 0x1 bash -c 'xl debug-keys C'
taskset 0x2 bash -c 'xl debug-keys C'


