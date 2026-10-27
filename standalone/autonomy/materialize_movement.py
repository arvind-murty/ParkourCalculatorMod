import io
from pathlib import Path
import subprocess
import tarfile


REVISION = 'f057c0c'


def materialize(root):
    root = Path(root)
    destination = root / 'standalone/autonomy/build/shared-movement'
    commit = subprocess.check_output(['git', 'rev-parse', REVISION + '^{commit}'], cwd=root, text=True).strip()
    stamp = destination / 'revision'
    if stamp.exists() and stamp.read_text().strip() == commit:
        return
    archive = subprocess.check_output(['git', 'archive', commit, 'core/src/main/java',
                                       'forge-core/src/main/java'], cwd=root)
    import shutil
    shutil.rmtree(destination, ignore_errors=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as sources:
        for entry in sources:
            if not entry.isfile() or not entry.name.endswith('.java'):
                continue
            if entry.name.startswith('forge-core/') and not entry.name.endswith((
                    '/FixedMovementCompiler.java', '/ReplayInputState.java', '/PlayerSprintMachine.java')):
                continue
            if entry.name.endswith('/StaticScene.java'):
                continue
            target = destination / entry.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(sources.extractfile(entry).read())
    stamp.write_text(commit + '\n')


if __name__ == '__main__':
    import sys
    materialize(sys.argv[1])
