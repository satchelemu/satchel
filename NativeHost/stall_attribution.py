#!/usr/bin/env python3
"""Per-frame first-use stall attribution from a frame TSV (DM_NVN_TSV, graphics-frames.tsv) and the
payload-free shader events (DM_SHADER_EVENTS, shader-events.tsv). Mac routes and phone trials alike.

Frame boundaries are the host's own present events ('frame') on the event clock. Each submit-thread
instant of a stalled frame goes to one category, innermost first, so nested time is never added twice.
'other' is the rest of the frame: guest work, GPU waits, pacing, loading."""
import bisect, json, sys

ORDER = [('library', {'library-use'}), ('translation', {'prepare-draw'}), ('lookahead-translation', {'prepare-lookahead'}),
         ('translation', {'prepare-use'}), ('descriptor', {'pipeline-descriptor'}), ('shader-setup', {'shader-load'}),
         ('pipeline', {'pipeline-use', 'pipeline-wait', 'pipeline-required-wait', 'pipeline-fallback', 'pipeline-ready', 'pipeline-capacity-wait'}),
         ('prediction', {'prediction'})]
RANK = {kind: i for i, (name, kinds) in enumerate(ORDER) for kind in kinds}
NAMES = list(dict.fromkeys(name for name, kinds in ORDER))

def read_frames(path):
    timings = {}
    for line in open(path).read().splitlines():
        cells = line.split('\t')
        if len(cells) == 3 and cells[1].startswith('@') and cells[2]:
            try: timings.setdefault(int(cells[0]), {})[cells[1]] = float(cells[2])
            except ValueError: pass
    return timings

def read_events(path):
    events = []
    for line in open(path).read().splitlines():
        ns, duration, thread, kind = line.split('\t'); events.append((int(ns), int(duration), int(thread), kind))
    return events

def summarize(timings, events, last_frame=None, threshold=50, top=12):
    submit = {e[2] for e in events if e[3] == 'submit-thread'}
    marks = [ns for ns, duration, thread, kind in events if kind == 'frame']
    spans = sorted((ns, ns + duration, RANK[kind]) for ns, duration, thread, kind in events if thread in submit and kind in RANK)
    frame_ms = {f: r['@frame_ms'] for f, r in timings.items() if '@frame_ms' in r and (last_frame is None or f <= last_frame)}
    ordered = sorted(frame_ms)
    # Align the k-th boundary with the frame rows by the interval series (the first may precede row 1).
    best = None
    for shift in range(0, 4):
        pairs = [(frame_ms[ordered[k]], (marks[k + shift] - marks[k + shift - 1]) / 1e6)
                 for k in range(1, min(len(ordered), len(marks) - shift)) if k + shift - 1 >= 0]
        if pairs:
            err = sorted(abs(x - y) for x, y in pairs)[len(pairs) // 2]
            if best is None or err < best[0]: best = (err, shift)
    rows = {}
    if best is not None:
        shift = best[1]; starts = [s[0] for s in spans]
        for k, f in enumerate(ordered):
            if k + shift < 1 or k + shift >= len(marks) or frame_ms[f] <= threshold: continue
            lo, hi = marks[k + shift - 1], marks[k + shift]
            cuts = {lo, hi}; active = []
            for s in spans[max(0, bisect.bisect_left(starts, lo) - 4096):]:
                if s[0] >= hi: break
                if s[1] > lo: active.append(s); cuts.add(max(lo, s[0])); cuts.add(min(hi, s[1]))
            cuts = sorted(cuts); parts = [0.0] * len(ORDER)
            for x, y in zip(cuts, cuts[1:]):
                inside = [s[2] for s in active if s[0] <= x and s[1] >= y]
                if inside: parts[min(inside)] += (y - x) / 1e6
            row = {'frame': f, 'ms': round(frame_ms[f], 1), 'libNew': int(timings[f].get('@lib_new', 0)), 'psoNew': int(timings[f].get('@pso_new', 0))}
            for name in NAMES: row[name] = round(sum(parts[j] for j, (n, _) in enumerate(ORDER) if n == name), 1)
            row['other'] = round(frame_ms[f] - sum(parts), 1)
            rows[f] = row
    slow = sorted(frame_ms.items(), key=lambda x: -x[1])
    return dict(over50=sum(ms > 50 for ms in frame_ms.values()), over100=sum(ms > 100 for ms in frame_ms.values()),
                worstMs=round(slow[0][1], 1) if slow else 0, worstFrames=[f for f, ms in slow[:3]],
                alignment=dict(shift=best[1], medianErrorMs=round(best[0], 3)) if best else None,
                over50ByPart={name: round(sum(r[name] for r in rows.values()), 1) for name in NAMES + ['other']},
                top=[rows[f] for f, ms in slow[:top] if f in rows])

if __name__ == '__main__':
    if len(sys.argv) not in (3, 4): raise SystemExit('usage: stall_attribution.py FRAMES_TSV EVENTS_TSV [LAST_FRAME]')
    print(json.dumps(summarize(read_frames(sys.argv[1]), read_events(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) == 4 else None), indent=1))
