#!/bin/zsh
# usage: ser.sh PORT SECONDS [command...]  - send commands and capture
# output for SECONDS seconds
port=$1; secs=$2; shift 2
# Configure the fd that stays open (USB-UART reverts to the default 9600bps
# when closed)
exec 3<>$port
stty 115200 raw -echo clocal <&3
# USB-UART (StickC Plus2's CH9102, /dev/cu.usbserial-*) reboots the device
# when the port is opened, so wait for it to boot before sending
if [[ $port == *usbserial* ]]; then boot=4; else boot=0.3; fi
( timeout $(( secs + boot )) cat <&3 ) &
sleep $boot
for c in "$@"; do print -r -- "$c" >&3; sleep 0.2; done
wait
exec 3>&-
