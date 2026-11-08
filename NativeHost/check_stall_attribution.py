#!/usr/bin/env python3
"""Synthetic check: stall_attribution.py attributes one launch only.

A warm repeat under the same trial tag used to append its events to the cold launch's file while the
frame file was rewritten, which misaligned every frame. The app now starts each launch's event file
with a 'launch' row; the script uses the events after the last one and refuses events that still span
more frames than the frame file holds. No game data: frames and events are generated here."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stall_attribution as sa

MS = 1_000_000

def launch(start, frames, slow, submit=7):
    """Frame rows and events of one launch: 31 to 35 ms frames, frame `slow` 400 ms with a 300 ms pipeline wait."""
    timings, events, now = {}, [(start, 0, submit, 'submit-thread')], start
    events.append((now, 0, submit, 'frame'))
    for f in range(1, frames + 1):
        ms = 400 if f == slow else 31 + (f * 7) % 5
        if f == slow: events.append((now + 50 * MS, 300 * MS, submit, 'pipeline-use'))
        now += ms * MS
        events.append((now, 0, submit, 'frame'))
        timings[f] = {'@frame_ms': float(ms), '@pso_new': 1.0 if f == slow else 0.0}
    return timings, events, now

def main():
    cold, cold_events, end = launch(10 * MS, 300, 120)
    warm, warm_events, _ = launch(end + 5000 * MS, 300, 200)
    alone = sa.summarize(warm, warm_events)
    assert alone['worstFrames'][0] == 200 and alone['top'][0]['pipeline'] == 300.0, alone
    # Appended without a mark (the old writer): refused, never silently misaligned.
    try:
        sa.summarize(warm, cold_events + warm_events)
        raise AssertionError('appended launches were accepted')
    except sa.MixedLaunches:
        pass
    # With the app's launch rows, only the last launch counts and gives the same result as alone.
    marked = [(5 * MS, 0, 1, 'launch')] + cold_events + [(end + 4000 * MS, 0, 1, 'launch')] + warm_events
    assert sa.summarize(warm, marked) == alone
    assert sa.last_launch(marked) == warm_events
    # One launch with no mark (Mac routes write a fresh file per boot) is unchanged.
    assert sa.summarize(cold, cold_events)['top'][0]['frame'] == 120
    # The translation ahead walk (TranslateAhead.inc) is prediction time; a library wait inside it stays library time.
    wait = next(e for e in cold_events if e[3] == 'pipeline-use')   # frame 120: 300 ms from 50 ms in
    walk = cold_events + [(wait[0] + 305 * MS, 40 * MS, 7, 'translate-rest'), (wait[0] + 315 * MS, 10 * MS, 7, 'library-use')]
    row = sa.summarize(cold, walk)['top'][0]
    assert row['frame'] == 120 and row['pipeline'] == 300.0 and row['prediction'] == 30.0 and row['library'] == 10.0, row
    print('PASS: stall attribution uses the last launch only and refuses appended launches, and counts the translation ahead walk as prediction (5 synthetic cases)')

if __name__ == '__main__':
    main()
