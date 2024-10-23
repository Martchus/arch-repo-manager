#!/bin/bash
echo "fake updatepkgsums: $@"
echo "# fake updatepkgsums ran on this file" >> PKGBUILD
exit 0
