# Kar-98K firing sound source

`user-kar98k-source.wav` is the replacement recording supplied by the user
(`Karbiner98K.wav`). The file in `resources.override/wav/semishoot(Kar-98K).wav` is
an exact byte-for-byte copy: no trimming, fade, volume adjustment or other
processing has been applied. The game itself converts stereo to mono and
applies its normal gun-source gain during playback.

Run `python3 tools/kar98k/generate_sound.py` to copy the original into the
runtime asset location. This uses only the Python standard library.
