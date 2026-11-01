"""Run with python3 tests/weapon_skin_scale_test.py (requires a C compiler)."""
from pathlib import Path
import subprocess
import tempfile
import struct
import zipfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / 'test'
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    str(root / 'tests/weapon_skin_scale_test.c'), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
source = (root / 'src/skins.c').read_text()
assert source.count('skin_reload_model(') == 4
assert 'category == SKIN_RIFLE || category == SKIN_SMG || category == SKIN_SHOTGUN' in source
# Guard against shrinking any currently bundled weapon skin.
with zipfile.ZipFile(root / 'bsresources.zip') as archive:
    for name in archive.namelist():
        if name.startswith(('kv6/semi(', 'kv6/smg(', 'kv6/shotgun(')) or name in (
            'kv6/semi.kv6', 'kv6/smg.kv6', 'kv6/shotgun.kv6'):
            data = archive.read(name)
            assert data[:4] == b'Kvxl'
            assert struct.unpack_from('<I', data, 8)[0] < 64, name
print('Weapon skin automatic scale checks passed')
