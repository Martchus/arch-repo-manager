#!/bin/bash
echo "fake gpg: $@"
echo "fake signature with GPG key ${@: -2}" > "${@: -1}.sig"
exit 0
