#!/usr/bin/env python3
"""Render portable SVG resource curves and numeric trends from soak CSV files."""
import csv
import html
import json
from pathlib import Path
import statistics
import sys


def slope(rows, field):
    xs = [float(r['seconds']) / 60 for r in rows]
    ys = [float(r[field]) for r in rows]
    if len(xs) < 2:
        return 0
    xm, ym = statistics.mean(xs), statistics.mean(ys)
    denominator = sum((x - xm) ** 2 for x in xs)
    return sum((x - xm) * (y - ym) for x, y in zip(xs, ys)) / denominator if denominator else 0


def render(folder):
    rows = list(csv.DictReader((folder / 'metrics.csv').open()))
    selected = [r for r in rows if r['phase'] in ('running', 'settled')]
    if not selected:
        return
    fields = [('footprint', 'Footprint (MiB)', 2 ** 20), ('rss', 'Resident memory (MiB)', 2 ** 20),
              ('metal', 'Metal allocated (MiB)', 2 ** 20), ('malloc', 'Malloc in use (MiB)', 2 ** 20),
              ('fds', 'File descriptors', 1), ('threads', 'Threads', 1), ('ports', 'Mach port names', 1)]
    colors = {'Castlevania': '#9f3c20', 'Odyssey': '#1467b0', 'TOTK': '#207647'}
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="1510" viewBox="0 0 1100 1510">',
           '<rect width="1100" height="1510" fill="white"/>',
           '<style>text{font-family:Helvetica,Arial,sans-serif;fill:#192535;font-size:13px}</style>',
           '<text x="70" y="28" font-size="22">Satchel session resource curves</text>',
           '<text x="70" y="51">Castlevania: rust. Odyssey: blue. TOTK: green. Circles: settled after stop.</text>']
    elapsed = json.loads((folder / 'result.json').read_text())['elapsed'] if (folder / 'result.json').exists() else 0
    xmax = max(elapsed, max(float(r['seconds']) for r in selected)) / 60
    for panel, (field, label, scale) in enumerate(fields):
        top = 86 + panel * 195
        ymin = 0
        ymax = max(float(r[field]) / scale for r in selected) * 1.08 or 1
        svg.append(f'<text x="70" y="{top - 10}">{html.escape(label)}</text>')
        for step in range(5):
            y = top + 140 - step * 35
            svg.append(f'<path d="M70 {y}H1040" stroke="#dde3e9"/>')
            svg.append(f'<text x="62" y="{y + 4}" text-anchor="end">{ymax * step / 4:.1f}</text>')
        for step in range(7):
            x = 70 + step * 970 / 6
            svg.append(f'<text x="{x:.1f}" y="{top + 159}" text-anchor="middle">{xmax * step / 6:.1f}</text>')
        svg.append(f'<text x="555" y="{top + 178}" text-anchor="middle">Elapsed minutes</text>')
        for cycle in sorted({r['cycle'] for r in selected}, key=int):
            group = [r for r in selected if r['cycle'] == cycle]
            points = [(70 + float(r['seconds']) / 60 / xmax * 970,
                       top + 140 - (float(r[field]) / scale - ymin) / ymax * 140) for r in group]
            color = colors[group[0]['title']]
            svg.append(f'<polyline fill="none" stroke="{color}" stroke-width="1.5" points="' +
                       ' '.join(f'{x:.1f},{y:.1f}' for x, y in points) + '"/>')
            for row, (x, y) in zip(group, points):
                if row['phase'] == 'settled':
                    svg.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{color}"/>')
    svg.append('</svg>')
    (folder / 'curves.svg').write_text('\n'.join(svg) + '\n')
    trends = []
    for cycle in sorted({r['cycle'] for r in selected}, key=int):
        group = [r for r in selected if r['cycle'] == cycle and r['phase'] == 'running']
        if not group:
            continue
        warm = [r for r in group if float(r['seconds']) >= float(group[0]['seconds']) + 300]
        trends.append(dict(cycle=int(cycle), title=group[0]['title'], samples=len(group),
                           warmSamples=len(warm), metrics={field: dict(
                               first=float(group[0][field]), last=float(group[-1][field]),
                               peak=max(float(r[field]) for r in group),
                               warmSlopePerMinute=slope(warm, field) if len(warm) >= 2 else None)
                               for field, _, _ in fields}))
    (folder / 'trends.json').write_text(json.dumps(trends, indent=2) + '\n')
    settled = [r for r in selected if r['phase'] == 'settled']
    # Same-process residuals expose accumulation hidden by each game's working
    # set. Preserve every stop, including the first cold baseline.
    residuals = dict(stops=[{key: row[key] for key in
                            ['cycle', 'title', 'seconds'] + [field for field, _, _ in fields]}
                           for row in settled], metrics={field: dict(
                               first=float(settled[0][field]), last=float(settled[-1][field]),
                               delta=float(settled[-1][field]) - float(settled[0][field]),
                               slopePerMinute=slope(settled, field))
                               for field, _, _ in fields} if settled else {})
    (folder / 'residuals.json').write_text(json.dumps(residuals, indent=2) + '\n')
    print(folder / 'curves.svg')


if __name__ == '__main__':
    for argument in sys.argv[1:]:
        render(Path(argument))
