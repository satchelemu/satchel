"""Retain symbolic allocation evidence without memory contents or guest frames."""
import re
import json
import subprocess
from pathlib import Path


def red_templates(engine):
    """Return source literals only. Runtime format arguments never enter a log."""
    templates = set()
    for directory in ['dm/host', 'dm/gfx/nvn']:
        for path in (Path(engine) / directory).iterdir():
            if path.suffix not in {'.c', '.m', '.h', '.inc'}:
                continue
            source = path.read_text()
            templates.update(re.findall(r'"\[RED\] ([^"\n]+)"', source))
            templates.update(re.findall(r'RED_ONCE\([^,]+, "([^"\n]+)"', source))
    return sorted((text.split('%', 1)[0], text) for text in templates
                  if len(text.split('%', 1)[0]) >= 8)


def red_category(line, templates):
    if '[RED]' not in line:
        return None
    for prefix, template in templates:
        if prefix in line:
            return template
    return 'Unclassified RED diagnostic'


def host_sample(pid):
    """Sample host and system symbols only, never guest addresses or images."""
    result = subprocess.run(['sample', str(pid), '1', '1', '-mayDie', '-file', '/dev/stdout'],
                            capture_output=True, text=True, timeout=10)
    frames = []
    allowed = re.compile(r'^(?:mac-stop-probe|libsystem_[A-Za-z0-9_.]+|libobjc\.A\.dylib|'
                         r'AGX\w+|Metal|MetalTools|Foundation|CoreFoundation|QuartzCore|'
                         r'libdispatch\.dylib|IOKit)$')
    for line in result.stdout.splitlines():
        match = re.match(r'([\s+!:|]*\d+\s+)(.*?) \(in ([^)]+)\)', line)
        if match and allowed.fullmatch(match[3]) and not match[2].startswith('???'):
            symbol = re.sub(r'0x[0-9a-fA-F]+', '<address>', match[2])
            frames.append(match[1] + symbol + ' (in ' + match[3] + ')')
    return dict(exit=result.returncode, frames=frames)


def helper_samples(pid):
    """Identify owned compiler waits without recording arguments or file paths."""
    listing = subprocess.check_output(['ps', '-axo', 'pid=,ppid=,comm='], text=True)
    table = {}
    for line in listing.splitlines():
        child, parent, executable = line.strip().split(None, 2)
        table[int(child)] = (int(parent), Path(executable).name)
    owned = {pid}
    while True:
        children = owned | {child for child, (parent, _) in table.items() if parent in owned}
        if children == owned:
            break
        owned = children
    records = []
    for child in sorted(owned - {pid}):
        parent, executable = table[child]
        record = dict(pid=child, parent=parent, executable=executable)
        try:
            record.update(host_sample(child))
        except subprocess.TimeoutExpired:
            record['error'] = 'Helper symbol sampling timed out after 10 seconds'
        records.append(record)
    return records


def allocation_metadata(output, host='mac-stop-probe'):
    rows = []
    for line in output.splitlines():
        if re.search(r'\d+ leaks for \d+ total leaked bytes', line):
            rows.append(line)
        elif re.match(r'\s*\d+ \(', line):
            rows.append(line)
        elif re.search(r'\b(?:' + re.escape(host) + r'|libobjc\.A\.dylib|libsystem_\w+\.dylib|'
                       r'AGX\w+|Metal|MetalTools|Foundation|CoreFoundation|QuartzCore)\b', line):
            # A full-stack row has an image and symbol. Reject paths and unknown
            # guest images even though leaks itself already disables contents.
            if '/' not in line and not re.search(r'\b(?:Binary Images|Path|Process):', line):
                rows.append(line)
    return re.sub(r'0x[0-9a-fA-F]+', '<address>', '\n'.join(rows)) + '\n'


def write_process_samples(folder, pid, prefix):
    """Keep bounded evidence before terminating a stalled diagnostic host."""
    for label, sampler in [('host', host_sample), ('helper', helper_samples)]:
        try:
            result = sampler(pid)
        except subprocess.TimeoutExpired:
            result = dict(error='Symbol sampling timed out after 10 seconds')
        (Path(folder) / (prefix + '-' + label + '-stacks.json')).write_text(
            json.dumps(result, indent=2) + '\n')
