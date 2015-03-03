#!/bin/bash

./prepare.sh
xl debug-keys M
sleep 100
xl debug-keys M
xl dmesg > raw_result
./stats.sh < raw_result
