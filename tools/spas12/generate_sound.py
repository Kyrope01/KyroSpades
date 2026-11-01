#!/usr/bin/env python3
"""Install the user-supplied SPAS-12 recording without modifying it."""
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[2]
source = root / 'tools/spas12/audio/user-spas12.wav'
dest = root / 'resources.override/wav/shotgunshoot(SPAS-12).wav'
dest.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(source, dest)
print(f'Copied {source} to {dest} unchanged')
