#!/usr/bin/env bash

set -x
set -e

DEV=/dev/ttyUSB0
RATE=460800
#RATE=1500000

idf.py build

while ! idf.py flash -p "$DEV" -b $RATE; do
  :;
done

idf.py monitor -p "$DEV"

