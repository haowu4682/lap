#!/bin/sh
qemu/qemu-system-x86_64 -m 8192M -device lap -drive \
cache=unsafe,file=../../lap/images/lap.qcow2 \
-nographic -simconfig test_image.conf \
-clock unix -snapshot
