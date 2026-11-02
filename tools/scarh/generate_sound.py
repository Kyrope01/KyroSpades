#!/usr/bin/env python3
"""Install the user-supplied SCAR-H recording without modifying it."""
from pathlib import Path
import shutil

root = Path(__file__).resolve().parents[2]
source = root / 'tools/scarh/audio/user-scarh.wav'
dest = root / 'resources.override/wav/smgshoot(SCAR-H).wav'
dest.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(source, dest)
print(f'Copied {source} to {dest} unchanged')
