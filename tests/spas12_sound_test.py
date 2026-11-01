"""Verify the SPAS-12 skin sound and its user-provided source."""
from pathlib import Path
import hashlib
import wave

root = Path(__file__).resolve().parents[1]
source = root / 'tools/spas12/audio/user-spas12.wav'
wav = root / 'resources.override/wav/shotgunshoot(SPAS-12).wav'
assert hashlib.sha256(source.read_bytes()).hexdigest() == '72fdb7df03efa38ca17855e89fdef5cca750c6b9e62b7434a40fc1f6e34de561'
assert wav.read_bytes() == source.read_bytes()
with wave.open(str(wav), 'rb') as f:
    assert (f.getnchannels(), f.getsampwidth(), f.getframerate(), f.getnframes()) == (2, 2, 44100, 51861)
assert '"shotgunshoot"' in (root / 'src/skins.c').read_text()
assert 'shotgunshoot(SPAS-12).wav' in (root / 'src/CMakeLists.txt').read_text()
print('SPAS-12 source, WAV format, and skin sound integration checks passed')
