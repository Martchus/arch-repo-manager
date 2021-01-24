#!/bin/bash
echo -e "printing some numbers\r" >&2
for i in {0001..5000}; do
    echo -e "line $i\r"
    [[ $1 ]] && sleep "$1"
done
exit 0
