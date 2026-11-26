#!/bin/sh
# one array (or several, comma list of spec.OVLE indexes) through the oracle, with the differences shown
# tp1.sh INDEXES [state] [frames]
T=x$1; ST=${2:-/home/devbox/work/states/rr_t3.st}; F=${3:-60}
SIX_ONLY=$1 SIX_OUT=/tmp/devbox-0/six_$T.z64 python3 /home/devbox/work/six/build_rom.py --oracle >/dev/null
SIX_ONLY=$1 TP_TAG=$T timeout 900 python3 /home/devbox/work/six/transplant.py $ST stock $F >/dev/null 2>&1
SIX_ONLY=$1 TP_TAG=$T timeout 900 python3 /home/devbox/work/six/transplant.py $ST /tmp/devbox-0/six_$T.z64 $F 2>&1 | grep -v "^\[\|plugin"
