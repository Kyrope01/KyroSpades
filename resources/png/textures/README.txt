Custom block textures (Textured Blocks feature)
================================================

Drop 16x16 (or any square) PNG files in this folder. When "Textured Blocks"
is enabled in the settings and this folder contains PNGs, the game textures
every block with the PNG whose overall average colour is closest to that
block's colour, instead of the built-in block atlas.

Requirements / notes:
  - Each PNG must be SQUARE (width == height). Non-square files are skipped
    (a warning is logged) and have no effect.
  - You can put as many PNGs here as you like; they are automatically packed
    into one atlas at load time.
  - The matching is by the AVERAGE COLOUR of the whole image, so give each
    texture a representative overall tone (e.g. a green PNG for grass blocks).
  - Changes here are picked up when the game (re)starts.
