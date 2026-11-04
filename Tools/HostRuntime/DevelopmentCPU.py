"""Explicit focused-only CPU provenance for host development, never promotion."""
from pathlib import Path
import hashlib, json

NAMES=set('CPUProbe.cpp CPUProbe.h Binary64.cpp Binary64.h build.py fixture.S native-fp-oracle.S test.cpp README.md VIXL-LICENCE vixl-memory-guard.patch'.split())

def validate(path,canonical,archive=None):
 digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 record_bytes=path.read_bytes()
 record=json.loads(record_bytes)
 assert record['kind']=='focused-development'
 root=Path(record['cpu_source_root']);assert root.is_absolute();root=root.resolve()
 sources=record['sources']
 assert set(sources)=={'Tools/CPUProbe/'+name for name in NAMES}
 inputs={str(path):hashlib.sha256(record_bytes).hexdigest()}
 for relative,want in sources.items():
  source=root/relative;assert digest(source)==want;inputs[str(source)]=want
 # Process is compiled from canonical source, so its public CPU ABI must match.
 canonical_header=canonical/'Tools/CPUProbe/CPUProbe.h'
 assert digest(root/'Tools/CPUProbe/CPUProbe.h')==digest(canonical_header)
 inputs[str(canonical_header)]=sources['Tools/CPUProbe/CPUProbe.h']
 expected=Path(record['host_archive']);assert expected.is_absolute()
 cpu=(archive or expected).resolve()
 assert digest(cpu)==record['artifacts'][str(expected)]
 inputs[str(cpu)]=record['artifacts'][str(expected)]
 proofs={}
 for name in ['source_checkpoint','focused','review']:
  proof=Path(record[name]['path']);assert proof.is_absolute()
  proof_bytes=proof.read_bytes()
  assert hashlib.sha256(proof_bytes).hexdigest()==record[name]['sha256']
  proofs[name]=json.loads(proof_bytes);inputs[str(proof)]=record[name]['sha256']
 checkpoint=proofs['source_checkpoint']
 assert set(checkpoint['files'])=={'source/'+p for p in sources}
 for relative,want in checkpoint['files'].items():
  p=Path(relative);assert p.parent==Path('source/Tools/CPUProbe')
  assert sources['Tools/CPUProbe/'+p.name]==want
 focused=proofs['focused'];review=proofs['review']
 assert type(focused['exit_code']) is int and focused['exit_code']==0
 assert type(focused['checks']) is int and focused['checks']>0
 assert focused['archive_sha256']==digest(cpu)
 assert focused['source_checkpoint_sha256']==record['source_checkpoint']['sha256']
 assert review['decision']=='clear'
 assert review['source_checkpoint_sha256']==record['source_checkpoint']['sha256']
 return root,cpu,inputs
