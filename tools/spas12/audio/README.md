# SPAS-12 firing sound

`user-spas12.wav` is the user's supplied `Spas12.wav` recording. The runtime
skin sound is an exact copy, with no conversion or audio processing. Run
`python3 tools/spas12/generate_sound.py` to reinstall it. The game downmixes
stereo to mono and applies its normal shotgun gain and pitch during playback.
