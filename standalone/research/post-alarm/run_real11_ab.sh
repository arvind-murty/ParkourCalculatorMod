#!/bin/zsh
# Real11 A/B of the case setting post_alarm on one source: arm A 'ipopt' (the fixture as is), arm B 'pipeline' (a copy
# with the setting). Run from standalone/; writes runs/post-alarm/real11-ab/.
# 1. fixed work: extended case capped at 100 solver calls, --profile-mode none, A/B alternated 3 times
# 2. extended case, 300 s (1024-call cap), --profile-mode bookkeeping, A then B
# 3. extended case with a 4096-call cap, 1800 s, --profile-mode bookkeeping, A then B
set -u
export PKC_IPOPT_PYTHON="$PWD/../.venv-guidance/bin/python" OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1
OUT=runs/post-alarm/real11-ab
FIXED=research/prefix-reuse-keys/2026-11-21/extended-fixedwork100.json
EXTENDED=cases/real11/extended-anchors.json
variant() {  # variant <source> <target> <post_alarm> [max_solver_calls]
  "$PKC_IPOPT_PYTHON" -c "import json,sys; d=json.load(open(sys.argv[1])); d['settings']['post_alarm']=sys.argv[3]
if len(sys.argv) > 4: d['settings']['max_solver_calls']=int(sys.argv[4])
json.dump(d, open(sys.argv[2], 'w'), indent=1)" "$@"
}
variant $FIXED $OUT/fixed-B.json pipeline
variant $EXTENDED $OUT/extended-B.json pipeline
variant $EXTENDED $OUT/long-A.json ipopt 4096
variant $EXTENDED $OUT/long-B.json pipeline 4096
run() {  # run <name> <input> <seconds> <profile mode>
  echo "$(date '+%H:%M:%S') $1 start, load $(uptime | sed 's/.*averages: //')"
  "$PKC_IPOPT_PYTHON" -B -m tools.profiling.run --input $2 --output $OUT/$1 --seconds $3 --profile-mode $4 > $OUT/$1.log 2>&1
  echo "$(date '+%H:%M:%S') $1 exit $?"
}
for i in 1 2 3; do
  run fixed-A$i $FIXED 900 none
  run fixed-B$i $OUT/fixed-B.json 900 none
done
run extended-A $EXTENDED 300 bookkeeping
run extended-B $OUT/extended-B.json 300 bookkeeping
run long-A $OUT/long-A.json 1800 bookkeeping
run long-B $OUT/long-B.json 1800 bookkeeping
