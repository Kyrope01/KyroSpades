#!/usr/bin/env python3
"""Install the user's Kar-98K sound unchanged into the game's skin assets."""
from pathlib import Path
from shutil import copyfile

root = Path(__file__).resolve().parents[2]
source = root / 'tools/kar98k/audio/user-kar98k-source.wav'
out = root / 'resources.override/wav/semishoot(Kar-98K).wav'
copyfile(source, out)
print(f'Copied {source} to {out} without modification')
