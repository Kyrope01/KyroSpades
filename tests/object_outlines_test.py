"""Source checks for the depth-based Object Outlines setting.

Run with: python3 tests/object_outlines_test.py
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text()
config = (root / "src/config.c").read_text()
header = (root / "src/config.h").read_text()
resource_config = (root / "resources/config.ini").read_text()

assert "int   object_outlines;" in header
assert "settings.object_outlines = 1;" in main
assert "object_outlines                = 1" in resource_config
assert "object_outline_thickness       = 0.7" in resource_config
assert "settings.object_outline_thickness = 0.7F;" in main
assert 'config_seti("client", "object_outlines", settings.object_outlines);' in config
assert 'config_setf("client", "object_outline_thickness", settings.object_outline_thickness);' in config
assert "IMPORT_SETTING(settings.object_outline_thickness, object_outline_thickness," in config
assert "fmaxf(0.5F, fminf(8.0F, atof(value)))" in config
assert ".float_min = 0.5F" in config
assert ".float_max = 8.0F" in config
assert '.value = &settings_tmp.object_outline_thickness' in config
assert '.numeric_input = 1' in config
assert 'if(a->max == INT_MAX || a->numeric_input)' in (root / "src/hud.c").read_text()
assert "IMPORT_SETTING(settings.object_outlines, object_outlines, atoi(value));" in config
assert '.value = &settings_tmp.object_outlines' in config
assert '.name = "Object outlines"' in config
assert "int outlines_postproc = settings.object_outlines;" in main
assert "|| hdr_postproc || bloom_postproc || outlines_postproc" in main
assert "outlines_postproc = 0;" in main
assert main.count('"uniform sampler2D depthmap;') >= 2
assert main.count('"uniform float pp_outlines;') == 2
assert main.count('"uniform float pp_outline_thickness;') == 2
assert main.count('"uniform float pp_fog_range;') == 2
assert "clamp(pp_outline_thickness, 0.5, 8.0)" in main
assert "min(thickness * 0.5, 1.0)" in main
assert "pp_depth_far - pp_depth_near" in main
assert "camera_mode == CAMERAMODE_SPECTATOR" in main
assert "? settings.spectator_fog_distance : settings.render_distance" in main
assert "(1.0 - fog)" in main
assert "smoothstep(0.025, 0.08" in main
assert "glUniform1i(postproc.uni_pp_depthmap, 2);" in main
assert "glBindTexture(GL_TEXTURE_2D, postproc.depth_tex);" in main
assert "glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture2);" in main
assert "settings.object_outlines && postproc.depth_tex && postproc.fbo" in main

print("Object Outlines setting and depth-edge pass checks passed")
