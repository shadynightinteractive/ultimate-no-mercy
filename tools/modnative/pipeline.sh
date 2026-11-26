#!/bin/sh
# tools/modnative/pipeline.sh (modnative7, 2026-11-14; from scratch/agents/modnative5-7): the native mod's proof, after
# build_mod.ps1 has built the mod (mod/build/mod_six) -- Stage 1 of that build (w/mod_aot), modrep's translation, the private
# data, the analysis, the mod's IR and debug information, the inventory, the census (native.py), the candidates (prove_gen.py
# put / check), the negative controls -- then lift.py prove of them all (tools/lift, LIFT_WORKERS, low priority). night.ps1
# runs it, then tools/aot/mod_native.py release and check. The rules the user approved on 2026-11-13/14 are on; their
# switches: prove_gen.py's head. NM_RULE_WRITES=1 (private values) only after its independent review.
#   sh tools/modnative/pipeline.sh <tag>
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DEV=$(cd "$HERE/../.." && pwd)
LIFT=$DEV/tools/lift
TAG=${1:-native}
export NM_MOD_DIR="${NM_MOD_DIR:-$DEV/mod}"
export LIFT_MOD_BUILD="$(cygpath -w "$NM_MOD_DIR/build/mod_six" 2>/dev/null || echo "$NM_MOD_DIR/build/mod_six")"
export LIFT_AOT_DIR="$(cygpath -w "$HERE/w/mod_aot" 2>/dev/null || echo "$HERE/w/mod_aot")"
export LIFT_PRIORITY=low
cd "$HERE"
mkdir -p w/mod_aot ir dwarf
python lowpri.py aot_copy.py "$NM_MOD_DIR/build/mod_six/nomercy_arcade.nrm" > pipe_aot.log 2>&1
(cd "$LIFT" && python "$HERE/lowpri.py" modrep.py > "$HERE/pipe_modrep.log" 2>&1)
(cd "$LIFT" && python "$HERE/lowpri.py" private_data.py > "$HERE/pipe_private.log" 2>&1)
(cd "$DEV" && python "$HERE/lowpri.py" "$LIFT/analyse.py" > "$HERE/pipe_analyse.log" 2>&1)
python lowpri.py gen_dwarf_ir.py "$NM_MOD_DIR" > pipe_dwarf.log 2>&1
python shrunk.py > pipe_shrunk.log 2>&1
python lowpri.py inventory.py > pipe_inventory.log 2>&1
python lowpri.py native.py census > pipe_census.log 2>&1
python dev/page_sites.py > pipe_page_sites.log 2>&1
python dev/page_pads.py > pipe_page_pads.log 2>&1
python lowpri.py prove_gen.py list > pipe_list.log 2>&1
python -c "import json;p=json.load(open('provable_now.json'));open('pipe_all.txt','w').write(' '.join(p['ok']))"
NM_CANDIDATES_BASE="$LIFT/candidates.json" python lowpri.py prove_gen.py put $(cat pipe_all.txt) > pipe_put.log 2>&1
python -c "import json;c=json.load(open('$LIFT/candidates.json'));open('pipe_put.txt','w').write(' '.join(sorted(k for k in c if k.startswith('modrep_nat_'))))"
python lowpri.py prove_gen.py check $(cat pipe_put.txt) > pipe_check.log 2>&1
python -c "import json;f=json.load(open('check_failed.json'));b=open('pipe_put.txt').read().split();open('pipe_checked.txt','w').write(' '.join(n for n in b if n not in f))"
python lowpri.py prove_gen.py neg > pipe_neg.log 2>&1
cat pipe_checked.txt > run_names.txt; echo >> run_names.txt; cat pipe_neg.log >> run_names.txt
echo "candidates: $(wc -w < pipe_checked.txt), negative controls: $(wc -w < pipe_neg.log)"
cd "$LIFT"
export FUZZ_MS=${FUZZ_MS:-300000} LIFT_WORKERS=${LIFT_WORKERS:-3} LIFT_EXTRA_ORIGINALS="$(cygpath -w "$LIFT/modrep/out/nat_originals.c" 2>/dev/null || echo "$LIFT/modrep/out/nat_originals.c")"
python lift.py prove "$TAG" $(cat "$HERE/run_names.txt") > "$HERE/prove_$TAG.log" 2>&1
cd "$HERE"
python m5_analyse.py "$LIFT/candidates.json" "$TAG" > "analyse_$TAG.txt" 2>&1
grep -m1 "PROVED" "analyse_$TAG.txt"
grep "^negative controls" "analyse_$TAG.txt"
