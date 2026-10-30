#!/bin/zsh
# usage: ser.sh PORT SECONDS [command...]  — コマンドを送り、SECONDS 秒間出力を取る
port=$1; secs=$2; shift 2
stty -f $port 115200 raw -echo
exec 3<>$port
( timeout $secs cat <&3 ) &
sleep 0.3
for c in "$@"; do print -r -- "$c" >&3; sleep 0.2; done
wait
exec 3>&-
