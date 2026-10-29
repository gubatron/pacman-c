#!/bin/bash

# run.sh

# Exit immediately if a command exits with a non-zero status.
set -e

cd "$(dirname "$0")"
make pacman
./pacman
