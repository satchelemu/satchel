#!/usr/bin/env python3
"""Attribute memory growth in soak_mac.py runs: which counter grows, in steps or steadily.

For every scene-memory field, census column and the sampled resident size, this
reports the value at the end of the scripted route, at the end of the run and at its
peak, the growth of each ten-minute window of the repeated movement loop, and the
least-squares slope over the second half of that loop. A counter is "steady" when it
grows in at least three quarters of the second-half windows and its second-half
slope exceeds the noise floor (8 MiB per hour for byte fields, six entries per hour
for counts) and its final value clears the first half's peak, so a counter that only
dips and recovers is not steady. Other net growth over the loop (above 8 MiB for bytes) is "step"; a net
fall is "falling"; otherwise "flat".
The guest VM accounting (vm.tsv, DM_MEMSTAT) adds per-class charged memory: guest,
malloc and gpu, plus the kernel's internal, compressed and graphics ledgers.
This classifies measurements; visual review of the soak screenshots decides whether
the repeated content stayed in one area.
Usage: soak_summary.py [--skip SECONDS] RUN [RUN...]  (writes soak-summary.json and .md in each RUN)
A RUN is a soak_mac.py run folder, or a folder copied from the phone holding
scene-memory.tsv and the DM_MEMSTAT file; there the loop starts after --skip seconds
(default 300) and frames map to seconds at the run's mean frame rate.
"""
from pathlib import Path
import csv
import json
import sys

WINDOW = 600   # seconds


def rows(path):
    if not path.is_file(): return []
    with path.open() as file:
        out = []
        for row in csv.DictReader(file, delimiter='\t'):
            try: out.append({k: int(float(v)) for k, v in row.items() if k is not None and v not in (None, '')})
            except ValueError: continue
        return out


def slope(points):
    if len(points) < 2: return 0.0
    n = len(points); mx = sum(x for x, _ in points) / n; my = sum(y for _, y in points) / n
    den = sum((x - mx) ** 2 for x, _ in points)
    return sum((x - mx) * (y - my) for x, y in points) / den if den else 0.0


def memstat_file(run):
    """The DM_MEMSTAT file: its first line is the engine's unit comment."""
    for path in sorted(run.glob('*.tsv')):
        with path.open(errors='replace') as file:
            if file.readline().startswith('# unit MiB'): return path
    return None


def summarize(run, skip=300):
    vm = memstat_file(run)
    if (run / 'route.json').is_file():
        route = json.loads((run / 'route.json').read_text())
        scene = run / 'memory.tsv'
        progress = rows(run / 'progress.tsv')
    else:
        # A folder copied from the phone: scene-memory.tsv (-SatchelMemoryProfile YES) and the
        # DM_MEMSTAT file. Without a frame clock, frames map to seconds at the run's mean rate.
        route = dict(game=run.name, variant='phone')
        scene = run / 'scene-memory.tsv'
        if vm is None or not scene.is_file(): raise SystemExit('Need scene-memory.tsv and the DM_MEMSTAT file in ' + str(run))
        seconds = [int(line.split('\t')[0]) for line in vm.read_text().splitlines() if line[:1].isdigit()]
        frames = [row['frame'] for row in rows(scene)]
        rate = max(frames) / max(seconds) if seconds and frames and max(seconds) else 1
        progress = [dict(seconds=s, frame=int(s * rate), rss=0) for s in seconds]
    if not progress: raise SystemExit('No progress samples: ' + str(run))
    # Map guest frames to run seconds through the progress samples (monotonic frames).
    def seconds_at(frame):
        for row in progress:
            if row['frame'] >= frame: return row['seconds']
        return progress[-1]['seconds']
    series = {}
    for row in rows(scene):
        t = seconds_at(row['frame'])
        for key, value in row.items():
            if key != 'frame': series.setdefault(key, []).append((t, row['frame'], value))
    for row in rows(run / 'census.tsv'):
        t = seconds_at(row['frame'])
        for key, value in row.items():
            if key != 'frame': series.setdefault('census:' + key, []).append((t, row['frame'], value))
    # DM_MEMSTAT: one row per second of host time (MiB). Classes are virtual/resident/charged.
    if vm:
        lines = [line.split('\t') for line in vm.read_text().splitlines() if line and not line.startswith('#')]
        header = lines[0] if lines else []
        for values in lines[1:]:
            if len(values) != len(header) or not values[0].isdigit(): continue
            t = int(values[0])
            for name, value in zip(header[1:], values[1:]):
                if '/' in value: name, value = name + '_charged', value.split('/')[-1]
                if name in ('available', 'headroom', 'other_charged', 'stack_charged', 'file_charged', 'code_charged'): continue
                try: series.setdefault('vm:' + name.replace(' ', '_') + '_bytes', []).append((t, 0, int(float(value)) * 2**20))
                except ValueError: pass
    for row in progress:
        if row['rss']: series.setdefault('rss', []).append((row['seconds'], row['frame'], row['rss']))
    marker = run / 'loop-start.json'
    if marker.is_file(): loop_start = json.loads(marker.read_text())['seconds']
    elif 'routeEnd' in route: loop_start = seconds_at(route['routeEnd'])
    else: loop_start = skip   # phone: exclude boot and the title's first load
    end = progress[-1]['seconds']
    half = loop_start + (end - loop_start) / 2
    report = {}
    for key, points in series.items():
        byte_field = key in ('rss',) or key.endswith('_bytes')
        loop = [(t, v) for t, _, v in points if t >= loop_start]
        if len(loop) < 4: continue
        windows = []
        edge = loop_start
        while edge < end:
            inside = [v for t, v in loop if edge <= t < edge + WINDOW]
            if inside: windows.append(dict(start=round(edge - loop_start), first=inside[0], last=inside[-1], max=max(inside)))
            edge += WINDOW
        deltas = [w['last'] - w['first'] for w in windows]
        late = [w for w in windows if w['start'] + loop_start >= half]
        late_growth = [w['last'] - w['first'] for w in late]
        late_slope = slope([(t, v) for t, v in loop if t >= half]) * 3600   # per hour
        floor = 8 * 2**20 if byte_field else 6
        growing = sum(d > (floor / 6 if byte_field else 0) for d in late_growth)
        # Steady growth keeps setting new highs: the end must also clear the first half's peak.
        early_peak = max((v for t, v in loop if t < half), default=loop[0][1])
        if late and growing >= 0.75 * len(late) and late_slope > floor and loop[-1][1] - early_peak > (floor / 6 if byte_field else 1): kind = 'steady'
        elif loop[-1][1] - loop[0][1] > (floor if byte_field else 0): kind = 'step'
        elif loop[-1][1] - loop[0][1] < -(floor if byte_field else 0): kind = 'falling'
        else: kind = 'flat'
        route_value = next((v for t, v in reversed([(t, v) for t, _, v in points]) if t <= loop_start), points[0][2])
        report[key] = dict(kind=kind, routeEnd=route_value, loopStart=loop[0][1], end=loop[-1][1], peak=max(v for _, _, v in points),
                           secondHalfSlopePerHour=round(late_slope, 1), windowDeltas=deltas, bytes=byte_field)
    order = {'steady': 0, 'step': 1, 'falling': 2, 'flat': 3}
    result = dict(run=str(run), game=route['game'], variant=route['variant'], seconds=end, loopStartSeconds=loop_start,
                  frames=progress[-1]['frame'], steady=sorted(k for k, v in report.items() if v['kind'] == 'steady'),
                  counters=dict(sorted(report.items(), key=lambda kv: (order[kv[1]['kind']], kv[0]))))
    (run / 'soak-summary.json').write_text(json.dumps(result, indent=2) + '\n')
    def show(value, byte_field): return '%.1f MiB' % (value / 2**20) if byte_field else str(value)
    lines = ['# %s soak, %s' % (route['game'], run.name), '',
             'Movement loop from %.0f s to %.0f s, %d guest frames. Window deltas are per %d seconds.' % (loop_start, end, result['frames'], WINDOW), '',
             '| Counter | Kind | Route end | Loop start | End | Peak | Late slope per hour | Window deltas |', '|---|---|---:|---:|---:|---:|---:|---|']
    for key, value in result['counters'].items():
        if value['kind'] == 'flat' and not key.startswith(('footprint', 'metal', 'rss')): continue
        b = value['bytes']
        lines.append('| %s | %s | %s | %s | %s | %s | %s | %s |' % (key, value['kind'], show(value['routeEnd'], b), show(value['loopStart'], b),
                     show(value['end'], b), show(value['peak'], b), show(value['secondHalfSlopePerHour'], b) if b else value['secondHalfSlopePerHour'],
                     ', '.join(show(d, b) for d in value['windowDeltas'])))
    (run / 'soak-summary.md').write_text('\n'.join(lines) + '\n')
    return result


if __name__ == '__main__':
    arguments = sys.argv[1:]
    skip = 300
    if arguments[:1] == ['--skip']: skip, arguments = int(arguments[1]), arguments[2:]
    if not arguments: raise SystemExit(__doc__)
    for argument in arguments:
        result = summarize(Path(argument), skip)
        print('%s %s: steady=%s' % (result['game'], Path(argument).name, ','.join(result['steady']) or 'none'))
