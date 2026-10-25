from pathlib import Path
import csv
import re
import gzip

out = Path(__file__).resolve().parent
trace = out / 'solver-trace.txt'
windows, objectives, rungs, stops, paths = [], [], [], [], []
current, active = None, None
trace_text = trace.read_text() if trace.exists() else gzip.decompress((out / 'solver-trace.txt.gz').read_bytes()).decode()
for number, line in enumerate(trace_text.splitlines(), 1):
    if 'LRS-WIN' in line:
        match = re.search(r'commit=(\d+) jumps=\[(\d+),(\d+)\) ticks=\[(\d+),(\d+)\).*replayTicks=(\d+)', line)
        if match:
            current = dict(commit=int(match[1]), first_jump=int(match[2])+1,
                           last_jump=int(match[3]), start_tick=int(match[4]), end_tick=int(match[5]),
                           commit_end=int(match[4])+int(match[6]), nominal_end=int(match[5]), extension=0, method='miss', ms='', line=number)
            windows.append(current)
            active = None
    if 'LRS-STUCK' in line:
        match = re.search(r'commit=(\d+) nextJump=(\d+) tick=(\d+)', line)
        if match:
            stops.append(dict(commit=int(match[1]), next_jump=int(match[2]), tick=int(match[3]), line=number))
    if current is None:
        continue
    if 'LRS-AIR' in line and 'nominalEnd=' in line:
        match = re.search(r'nominalEnd=(\d+) objectiveTick=(\d+) extension=(\d+)', line)
        if match:
            current['nominal_end'] = int(match[1])
            current['extension'] = int(match[3])
    if 'LRS-OBJ' in line and 'begin ' in line:
        stage = line.split('begin ', 1)[1]
        match = re.search(r'[XZ]/(?:MAX|MIN)', stage)
        method = 'SLP' if 'SLP' in stage else 'terminal recovery' if 'terminal' in stage else match[0] if match else stage
        active = {key: current[key] for key in ['commit', 'first_jump', 'last_jump', 'start_tick', 'end_tick', 'nominal_end', 'extension']}
        active.update(method=method, outcome='', ms='', line=number)
        objectives.append(active)
    if active is not None and 'CF ' in line and 'rung margin=' in line:
        match = re.search(r'margin=([^ ]+) iters=(\d+).*viol=([^ ]+) ms=([\d.]+)', line)
        if match:
            row = {key: current[key] for key in ['commit', 'first_jump', 'last_jump']}
            row.update(method=active['method'], margin=match[1], iterations=match[2], violation=match[3], ms=match[4], line=number)
            rungs.append(row)
    if active is not None and 'LRS-OBJ' in line and 'end ' in line:
        match = re.search(r'result=(\w+) ms=([\d.]+)', line)
        if match:
            active.update(outcome=match[1], ms=match[2])
            if match[1] == 'solved':
                current['method'] = active['method']
    if 'LRS ' in line and 'window i=' in line:
        match = re.search(r' -> (solved|miss) ms=([\d.]+)', line)
        if match:
            current['ms'] = match[2]
        if 'cached -> solved' in line:
            current['method'] = 'cached'
    if 'LRS-COMMIT-PATH' in line:
        match = re.search(r'jump=(\d+) tick=(\d+) x=([^ ]+) z=([^ ]+) vx=([^ ]+) vz=([^ ]+) yaw=([^ ]+)', line)
        if match:
            paths.append(dict(commit=current['commit'], block_start_jump=int(match[1])+1, tick=int(match[2]),
                              x=match[3], z=match[4], vx=match[5], vz=match[6], yaw=match[7], line=number))
for name, rows in [('windows.csv', windows), ('objectives.csv', objectives), ('cf-rungs.csv', rungs),
                   ('stops.csv', stops), ('committed-ticks.csv', paths)]:
    if rows:
        with (out / name).open('w') as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
            writer.writeheader()
            writer.writerows(rows)
lines = ['# Every attempted window, free-air objective placement', '', 'Human jump ranges are nominal, one-based and inclusive; the tick horizon can extend farther to find a free airborne state. A failed window commits nothing.', '']
for commit in [1]:
    lines += [f'## Commit{commit}', '', '| Nominal jumps | Input states | Nominal end | Extension | Accepted method | Commit exit state | Window ms |',
              '|---|---|---:|---:|---|---:|---:|']
    for row in windows:
        if row['commit'] == commit:
            exit_tick = row['commit_end'] if row['method'] != 'miss' else '-'
            lines.append(f"| {row['first_jump']}–{row['last_jump']} | {row['start_tick']}–{row['end_tick']} | {row['nominal_end']} | {row['extension']} | {row['method']} | {exit_tick} | {row['ms']} |")
(out / 'windows.md').write_text('\n'.join(lines) + '\n')
previous_path = out.parent / 'full-route-free-air-ten-ladder-2026-10-24/windows.csv'
previous = [row for row in csv.DictReader(previous_path.open()) if row.get('run', 'current') == 'current']
lines = ['# Commit1: decoder fallback enabled versus disabled', '', 'Only decoder fallback changes; committed states can diverge. These are policy comparisons, not identical-entry controls. A number before a method is the window size; × means that window failed.', '']
for commit in [1]:
    lines += [f'## Commit{commit}', '', '| Starting jump | Fallback enabled | Fallback disabled |', '|---:|---|---|']
    starts = sorted({int(row['first_jump']) for row in windows+previous if int(row['commit']) == commit})
    for start in starts:
        cells = []
        for rows in [previous, windows]:
            values = [str(int(row['last_jump'])-start+1)+('×' if row['method']=='miss' else ': '+row['method'])
                      for row in rows if int(row['commit'])==commit and int(row['first_jump'])==start]
            cells.append('<br>'.join(values) or '-')
        lines.append(f'| {start} | ' + ' | '.join(cells) + ' |')
(out / 'comparison.md').write_text('\n'.join(lines) + '\n')
print('windows', len(windows), 'objectives', len(objectives), 'stops', stops)
