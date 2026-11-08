"""Source checks for the requested first-run graphics defaults."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text()
config = (root / "resources/config.ini").read_text()

assert "settings.multisamples = 2;" in main
assert "multisamples                   = 2" in config
assert "settings.object_outlines = 0;" in main
assert "object_outlines                = 0" in config
assert "settings.object_outline_thickness = 0.7F;" in main
assert "object_outline_thickness       = 0.7" in config
assert "settings.killfeed_distance = 1;" in main
assert "killfeed_distance              = 1" in config
assert "settings.player_technical_stats = 1;" in main
assert "player_technical_stats         = 1" in config
assert "settings.show_live_player_count = 1;" in main
assert "show_live_player_count         = 1" in config
assert "settings.lighten_colors = 120;" in main
assert "lighten_colors                 = 120" in config
assert "settings.contrast = 0.0F;" in main
assert "settings.vignette = 0.0F;" in main
assert "contrast                       = 0.0" in config
assert "vignette                       = 0.0" in config
assert main.index("settings.exposure = 3.0F;") < main.index("        config_reload();")
assert "settings.exposure = 3.0F;" in main
assert "settings.saturation = -22.42F;" in main
assert "exposure                       = 3.00" in config
assert "saturation                     = -22.42" in config
assert "settings.camera_fov = 90.0F;" in main
assert "camera_fov                     = 90.00000" in config
assert main.index("settings.camera_fov = 90.0F;") < main.index("        config_reload();")
assert "#define CAMERA_DEFAULT_FOV 70.0F" in (root / "src/camera.h").read_text()
for weapon in ("rifle", "shotgun", "smg"):
    assert f"settings.{weapon}_ads_fov = CAMERA_DEFAULT_FOV;" in main
assert "settings.ads_zoom_animation = 0;" in main
assert "ads_zoom_animation             = 0" in config

assert "settings.filmic_tonemapping = 0;" in main
assert "settings.filmic_tonemapping = 1;" not in main
assert "settings.chromatic_aberration = 0;" in main
assert "settings.chromatic_aberration = 1;" not in main
assert "filmic_tonemapping             = 0" in config
assert "chromatic_aberration           = 0" in config

print("Default graphics settings passed")
