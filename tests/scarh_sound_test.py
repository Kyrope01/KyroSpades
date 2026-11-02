"""Verify the SCAR-H skin sound and its user-provided source."""
from pathlib import Path
import hashlib
import wave

root = Path(__file__).resolve().parents[1]
source = root / 'tools/scarh/audio/user-scarh.wav'
wav = root / 'resources.override/wav/smgshoot(SCAR-H).wav'
assert hashlib.sha256(source.read_bytes()).hexdigest() == '16f5f37ce33ac770efe553c63f6eea8cf3853541e4611c6e5e9365174bcc8133'
assert wav.read_bytes() == source.read_bytes()
with wave.open(str(wav), 'rb') as f:
    assert (f.getnchannels(), f.getsampwidth(), f.getframerate(), f.getnframes()) == (2, 2, 44100, 62899)
assert '"smgshoot"' in (root / 'src/skins.c').read_text()
assert 'smgshoot(SCAR-H).wav' in (root / 'src/CMakeLists.txt').read_text()
print('SCAR-H source, WAV format, and skin sound integration checks passed')
