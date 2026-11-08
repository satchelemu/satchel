"""Texture-write watch for the app's boot feedback: hle_nvn.c counts textures the CPU writes after
their first GPU use (texture-writes.patch), with nvn_mtl.m telling how the replay holds a texture
(texture-writes-metal.patch). Applied last to each copy, without editing the pinned engine."""
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
RESET_ANCHOR = '    free(g_ft); g_ft = NULL; g_nft = g_cft = 0;\n'


def host(path):
    """Patch an overlay copy of hle_nvn.c in place; wire the managed session reset when present."""
    path = Path(path)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', HERE / 'texture-writes.patch'], cwd=path.parent, check=True)
    text = path.read_text()
    if 'int satchel_nvn_session_reset(void) {' in text:
        if text.count(RESET_ANCHOR) != 1:
            raise RuntimeError('Texture-write watch needs the session reset of the flush-upload list')
        text = text.replace(RESET_ANCHOR, RESET_ANCHOR + '    satchel_tw_reset();\n', 1)
        path.write_text(text)
    return path


def metal(path):
    """Patch an overlay copy of nvn_mtl.m in place."""
    path = Path(path)
    subprocess.run(['patch', '-p1', '--fuzz=0', '-i', HERE / 'texture-writes-metal.patch'], cwd=path.parent, check=True)
    return path
