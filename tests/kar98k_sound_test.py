"""Verify that the Kar-98K sound is the exact WAV supplied by the user."""
from pathlib import Path
import hashlib
import wave

root = Path(__file__).resolve().parents[1]
source = root / 'tools/kar98k/audio/user-kar98k-source.wav'
output = root / 'resources.override/wav/semishoot(Kar-98K).wav'
expected = 'e5a7f646de7772e0df359284d546391ea24bf42cad7762c6010ebd474995db1f'
assert hashlib.sha256(source.read_bytes()).hexdigest() == expected
assert output.read_bytes() == source.read_bytes()
with wave.open(str(output), 'rb') as wav:
    assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) == (2, 2, 44100, 35456)
assert 'semishoot' not in (root / 'tools/kar98k/generate.py').read_text()
print('Kar-98K user recording is installed byte-for-byte')
