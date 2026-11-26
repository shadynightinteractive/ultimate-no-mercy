#!/bin/sh
# transplant oracle over several match states: oracle.sh SIX_ROM [frames]
F=${2:-600}
for st in /tmp/devbox-0/rg_van.pkl.bell.st /home/devbox/work/states/rr_t1.st /home/devbox/work/states/rr_t3.st /home/devbox/work/states/rr_t8.st; do
  timeout 1200 python3 /home/devbox/work/six/transplant.py $st stock $F >/dev/null 2>&1
  echo "$(basename $st): $(timeout 1200 python3 /home/devbox/work/six/transplant.py $st $1 $F 2>&1 | grep -E 'compared|RAM |isn' | tr '\n' ' ')"
done
