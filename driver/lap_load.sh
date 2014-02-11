#!/bin/bash
/sbin/insmod ./lap.ko || exit 1
major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
rm -f /dev/lap
mknod /dev/lap c $major 0
#chgrp $group /dev/lap
#chmod $mode  /dev/lap

