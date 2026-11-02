#!/bin/sh
# Send console lines to the daemon started by `oh-serial.py --daemon build/serial`
# and print what came back.   scripts/oh-cmd.sh "oh info" ["oh out 0 1" ...]
PREFIX="${OH_SERIAL:-build/serial}"
MARK="$(wc -c < "$PREFIX.log")"
for line in "$@"; do
    printf '%s\n' "$line" > "$PREFIX.fifo"
    sleep "${OH_CMD_WAIT:-1.5}"
done
tail -c +"$((MARK + 1))" "$PREFIX.log" | sed 's/\x1b\[[0-9;]*[A-Za-z]//g'
