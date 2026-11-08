"""Frame-scripted routes and capture checkpoints shared by run_mac.py and soak_mac.py."""
from pathlib import Path

ROUTES = {
    'Castlevania': Path(__file__).with_name('castlevania-route.txt').read_text().strip(),
    'Odyssey': 'f900:A~f9,f1200:A~f9,f1500:A~f9,f2100:A~f9,f3600:Plus~f9,f3900:B~f9,f4500:LUp~f90,f5000:RRight~f120,f6000:Plus~f9,f6300:B~f9',
    'TOTK': 'f900:A~f9,f1200:A~f9,f1500:A~f9,f4200:Plus~f9,f4500:B~f9,f5100:LUp~f90,f6000:Plus~f9,f6300:B~f9',
}
FRAMES = {'Castlevania': 9000, 'Odyssey': 7200, 'TOTK': 7200}


def checkpoints(game, frames):
    points = sorted({300, 900, 1800, 2400, 3600, 4800, 6000, frames})
    if game == 'Castlevania': points = sorted(set(points) | {6900, 7250, 7295, 8015})
    if game == 'Odyssey': points = sorted(set(points) | {3700, 4000, 5200, 6100, 6400})
    if game == 'TOTK': points = sorted(set(points) | {4300, 4600, 6100, 6400})
    return [frame for frame in points if frame <= frames]
