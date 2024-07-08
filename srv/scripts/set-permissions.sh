#!/bin/bash
set -e

local_db_path=${1:-$PWD}
sudo chown -vR "$USER:buildservice-git" "$local_db_path"
find "$local_db_path" -type d -exec chmod 2775 {} \+
find "$local_db_path" -type f -exec chmod 664 {} \+
