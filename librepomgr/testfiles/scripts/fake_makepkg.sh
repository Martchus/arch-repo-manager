#!/bin/bash
if [[ $1 == '--printsrcinfo' ]]; then
    if [[ -e '.SRCINFO' ]]; then
        cat '.SRCINFO'
        exit 0
    fi
    echo ".SRCINFO does not exist, working dir was '$PWD'"
    exit 1
fi
echo "fake makepkg: $@"
exit 0
