#!/usr/bin/env python3
"""Join bounded host traces to frame gaps without double counting nested spans."""
import argparse
from bisect import bisect_left
from collections import defaultdict
import csv, json, math, struct
from pathlib import Path

FS_NAMES = {0:'romfs_read',1:'destination_prepare',2:'pread',3:'open',4:'path',5:'path_lock',6:'mapped_copy',7:'map_window',8:'other_read',9:'file_size',10:'close',11:'entry_type',12:'open_directory',13:'read_directory',14:'directory_count',15:'close_directory'}
WAIT = {0x18,0x1c,0x1d,0x34,0x35}

def trace(path):
    data=path.read_bytes()
    if len(data)<64: raise ValueError('Truncated trace header')
    magic,epoch,count,capacity=struct.unpack_from('<4Q',data)
    if magic!=0x31524354534c50 or len(data)!=64+capacity*48: raise ValueError('Invalid trace format')
    rows=[]
    for i in range(min(count,capacity)):
        start,cpu,thread,code,size,end=struct.unpack_from('<6Q',data,64+i*48)
        if end>=start and end:
            rows.append(dict(start=(start-epoch)/1e6,end=(end-epoch)/1e6,cpu=cpu/1e6,thread=thread,code=code,bytes=size))
    return rows,dict(reserved=count,committed=len(rows),capacity=capacity,overflow=max(0,count-capacity),complete=count==len(rows))

def union(intervals):
    total=0.; left=right=None
    for a,b in sorted(intervals):
        if b<=a: continue
        if left is None: left,right=a,b
        elif a>right: total+=right-left; left,right=a,b
        else: right=max(right,b)
    return total+(right-left if left is not None else 0.)

def measure(rows,lo=-math.inf,hi=math.inf):
    hits=[r for r in rows if r['start']<hi and r['end']>lo]
    complete=[r for r in hits if r['start']>=lo and r['end']<=hi]
    intervals=[(max(lo,r['start']),min(hi,r['end'])) for r in hits]
    return dict(calls=len(hits),wall_sum_ms=sum(b-a for a,b in intervals),wall_union_ms=union(intervals),
                cpu_lower_ms=sum(r['cpu'] for r in complete),cpu_upper_ms=sum(r['cpu'] for r in hits),
                complete_call_bytes=sum(r['bytes'] for r in complete))

def summarize(paths,frames,threshold):
    groups=defaultdict(list); health={}
    for path in paths:
        unit=path.name.split('.')[-2]; rows,health[unit]=trace(path)
        for r in rows:
            name=FS_NAMES.get(r['code'],str(r['code'])) if unit=='fs' else f"svc_{r['code']:02x}"
            groups[name].append(r)
            if (unit=='fs' and r['code'] in {0,3,8,9,10,11,12,13,14,15}) or (unit=='svc' and r['code'] not in WAIT): groups['host_outer'].append(r)
            if unit=='svc' and r['code'] in WAIT: groups['blocking_svc'].append(r)
    metrics=defaultdict(dict)
    with frames.open() as f:
        for line in f:
            parts=line.strip().split('\t')
            if len(parts)==3 and parts[0].isdigit() and parts[1].startswith('@'):
                try: metrics[int(parts[0])][parts[1][1:]]=float(parts[2])
                except ValueError: pass
    pauses=[]
    ordered={name:sorted(rows,key=lambda r:r['start']) for name,rows in groups.items()}
    starts={name:[r['start'] for r in rows] for name,rows in ordered.items()}
    longest={name:max((r['end']-r['start'] for r in rows),default=0) for name,rows in ordered.items()}
    for frame,m in sorted(metrics.items()):
        if m.get('frame_ms',0)<threshold or 'call_ms' not in m: continue
        hi=m['call_ms'];lo=hi-m['frame_ms']; selected={}
        for name,rows in ordered.items():
            subset=rows[bisect_left(starts[name],lo-longest[name]):bisect_left(starts[name],hi)]
            result=measure(subset,lo,hi)
            if result['calls']: selected[name]=result
        pauses.append(dict(frame=frame,start_ms=lo,end_ms=hi,metrics=m,host=selected))
    return dict(trace_health=health,notes=[
        'CPU bounds are exact for fully contained calls; boundary calls contribute only to the upper bound.',
        'Wall sums overlap across threads. Wall union is elapsed time covered by at least one recorded call.',
        'Read/open include child scopes. host_outer includes these once plus nonblocking memory/GC SVCs.',
        'Blocking SVC wall time includes intentional guest waits and OS scheduling; it is not removable host CPU.',
        'SVC waits shorter than 1 ms are omitted from events. Thread scheduling outside calls requires separate sampling.',
        'These traces perturb execution and are attribution only. Overflow or unfinished calls limit coverage.'
    ],totals={name:measure(rows) for name,rows in ordered.items()},pauses=pauses)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('frames',type=Path);p.add_argument('traces',nargs='+',type=Path);p.add_argument('--threshold-ms',type=float,default=100)
    a=p.parse_args(); print(json.dumps(summarize(a.traces,a.frames,a.threshold_ms),indent=2))
if __name__=='__main__':main()
