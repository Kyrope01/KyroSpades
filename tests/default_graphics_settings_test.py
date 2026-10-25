"""Source checks for the requested first-run graphics defaults."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text()
config = (root / "resources/config.ini").read_text()

assert "settings.multisamples = 2;" in main
assert "multisamples                   = 2" in config
assert "settings.object_outline_thickness = 1.0F;" in main
assert "object_outline_thickness       = 1.0" in config
assert "settings.exposure = 20.0F;" in main
assert "settings.saturation = -15.0F;" in main
assert "exposure                       = 20.0" in config
assert "saturation                     = -15.0" in config

print("Default graphics settings passed")
