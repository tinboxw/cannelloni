#!/bin/bash

#CURR_DIR=$(pwd)
SCRIPT_POS=$(readlink -f $0)
SCRIPT_DIR=$(dirname "$SCRIPT_POS")
EXEC_BIN=${SCRIPT_DIR}/../bin/cannelloni

ip link set can0 down
ip link set can1 down

ip link set can0 up type can bitrate 500000
ip link set can1 up type can bitrate 500000

echo "Startup ${EXEC_BIN} -I can0 -R 127.0.0.1 -r 9996 -l 5200"
${EXEC_BIN} -I can0 -R 127.0.0.1 -r 9996 -l 5200 & #-d cubt &

echo "Startup ${EXEC_BIN} -I can1 -R 127.0.0.1 -r 9997 -l 5100"
${EXEC_BIN} -I can1 -R 127.0.0.1 -r 9997 -l 5100 & #-d cubt &

wait