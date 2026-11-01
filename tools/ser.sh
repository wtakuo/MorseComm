#!/bin/zsh
# usage: ser.sh PORT SECONDS [command...]  — コマンドを送り、SECONDS 秒間出力を取る
port=$1; secs=$2; shift 2
# 開いたままの fd に対して設定する (USB-UART は閉じると設定が既定 9600bps に戻るため)
exec 3<>$port
stty 115200 raw -echo clocal <&3
( timeout $secs cat <&3 ) &
# USB-UART (StickC Plus2 の CH9102, /dev/cu.usbserial-*) はポートを開くと本体が再起動するので、起動を待ってから送る
if [[ $port == *usbserial* ]]; then sleep 2.5; else sleep 0.3; fi
for c in "$@"; do print -r -- "$c" >&3; sleep 0.2; done
wait
exec 3>&-
