"""Source checks for the requested first-run graphics defaults."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text()
config = (root / "resources/config.ini").read_text()

assert "settings.multisamples = 2;" in main
assert "multisamples                   = 2" in config
assert "settings.object_outlines = 1;" in main
assert "object_outlines                = 1" in config
assert "settings.object_outline_thickness = 0.7F;" in main
assert "object_outline_thickness       = 0.7" in config
assert "settings.player_technical_stats = 1;" in main
assert "player_technical_stats         = 1" in config
assert "settings.show_live_player_count = 1;" in main
assert "show_live_player_count         = 1" in config
assert "settings.exposure = 20.0F;" in main
assert "settings.saturation = -15.0F;" in main
assert "exposure                       = 20.0" in config
assert "saturation                     = -15.0" in config
assert "settings.bloom = 0;" in main
assert "bloom                          = 0" in config
assert "settings.ads_zoom_animation = 0;" in main
assert "ads_zoom_animation             = 0" in config

print("Default graphics settings passed")
