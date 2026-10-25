"""Source regression checks for first-person viewmodel shadow handling."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
player = (root / "src/player.c").read_text()
model = (root / "src/model.c").read_text()

render = player[player.index("void player_render("):]
viewmodel_start = render.index("bool unshadowed_viewmodel = id == local_player_id && camera_mode == CAMERAMODE_FPS;")
viewmodel_end = render.index("if(!render_fpv)\n\t\tplayer_render_flashlight_tip(id);", viewmodel_start)
viewmodel = render[viewmodel_start:viewmodel_end]

assert "if(unshadowed_viewmodel)\n\t\tkv6_calclight_viewmodel();" in viewmodel
assert "kv6_render(zombie_team && model_zombie_arms.voxels" in viewmodel
assert "case TOOL_GUN:" in viewmodel
assert "if(unshadowed_viewmodel)\n\t\tkv6_calclight(p->pos.x, p->pos.y, p->pos.z);" in viewmodel
assert "void kv6_calclight_viewmodel(void)" in model
assert "kv6_set_light_scale(1.0F);" in model

print("First-person viewmodels bypass world sunblock and restore player lighting")
