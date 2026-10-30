# Legacy stepping investigation

Planning evidence, 2026-10-29. The current Forge simulator delegates collision movement to Minecraft. The investigation compared the cached mapped Minecraft/Forge 1.8.9 Entity step branch with `core/.../replay/StaticScene.java`, the scalar `standalone/ipopt/ladder.py` step helper and orchestration's existing profiles.

The local cached source artifact was `forgeBin-1.8.9-11.15.1.1722.jar`, SHA-256 `92d94f1392f1c2ecc10d89f47f2584127b66d129cb3da76a4cb94a0c2fa04802`. Entity/EntityLivingBase bytecode was inspected and decompiled with the locally cached Vineflower 1.11.0. No Minecraft source is copied here. The mapped Entity movement method is named `move`; it is the legacy `moveEntity` collision procedure. The step branch matches the shared resolver's eligibility, original-box reset, A/B paths and progress/tie selection.

`StepMechanicsProbe.java` is an independent caller of the existing shared resolver. `probe-results.txt` contains its output. The original six probes were expanded and relabeled against the user terminology: normal step, slab boost, blip, blip-up and grind, plus proposed landing/jump step-over and partial-rise cancellation cases, and a return-leg equality boundary case. Descriptive labels for proposed cases are not established community names.

The unsupported and equality examples show why final feet height, actual support, ground state and carried Y must be represented separately. The user wants explicit movement-case schedules and constraints rather than a reproduction of this internal resolver. See STEPPING_CASES.md for the authoritative terminology and which additional cases remain under discussion. These are collision probes, not fresh full-route IPOPT or game replay witnesses. No production movement or acceptance code changed during this investigation. See [STEPPING_PLAN.md](../../STEPPING_PLAN.md) for the proposed implementation and tests.

From the isolated worktree, after the existing compiler config/classes are available:

```sh
python3 - <<'PY'
import json
from pathlib import Path
import subprocess
import tempfile

command = json.loads(Path('standalone/autonomy/build/compiler.json').read_text())['command']
classpath = command[command.index('-cp') + 1]
source = Path('standalone/autonomy/experiments/stepping-mechanics/StepMechanicsProbe.java')
with tempfile.TemporaryDirectory(prefix='stepping-probe-') as output:
    subprocess.run([str(Path(command[0]).with_name('javac')), '-cp', classpath, '-d', output, str(source)], check=True)
    subprocess.run([command[0], '-cp', output + ':' + classpath, 'StepMechanicsProbe'], check=True)
PY
```
