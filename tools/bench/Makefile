# Headless CPU benchmarks for the renderer's hot CPU passes.
#
#   make -C tools/bench          # build
#   make -C tools/bench run      # build and run
#   make -C tools/bench check    # syntax/warning check of the GL-facing TUs
#
# No GL headers, window system or GPU are required: tools/bench/stubs supplies
# the small surface the benchmarked translation units touch.

CC      ?= cc
CFLAGS  ?= -O2 -g
# Three warning classes are suppressed, all for code this harness compiles
# but does not own:
#   -Wno-unused-parameter  event/callback signatures must match their APIs
#   -Wno-sign-compare      mixed int/size_t loops throughout the engine
#   -Wno-type-limits       player.h's local_player_id is an unsigned char and
#                          PLAYERS_MAX is 256, so the "id in range" guards are
#                          provably dead; they are kept as defensive checks
# Everything else - unused functions and variables, -Wswitch, signedness,
# missing initialisers - is a hard failure. This target is the only place
# the GL-facing translation units are compiled without a GPU.
CFLAGS  += -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-type-limits

ROOT    := ../..
ENGINE  := $(ROOT)/src
STUBS   := stubs
BUILD   := build
INC     := -I$(ENGINE) -I$(ROOT)/deps -I$(ROOT)/deps/libvxl -I$(STUBS)

OBJS := build/bench.o \
        build/water.o build/chunk.o build/tesselator.o \
        build/channel.o build/hashtable.o build/libvxl.o \
        build/bench_stubs.o

all: bench

$(BUILD):
	mkdir -p $(BUILD)

build/bench.o: bench.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/bench_stubs.o: $(STUBS)/bench_stubs.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/%.o: $(ENGINE)/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

build/libvxl.o: $(ROOT)/deps/libvxl/libvxl.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

bench: $(OBJS)
	$(CC) $(OBJS) -o bench -lm -lpthread

run: bench
	./bench

# Compile-only check for the translation units that normally need a real GL
# or enet header. The stubs are enough to type-check them, which catches the class of
# mistake this harness exists for (typos, wrong GL enums, bad renames) without
# a GPU. hud.c/network.c/ping.c/demo.c are covered too: stubs/enet/enet.h
# supplies the enet surface they include, purely for type checking.
CHECK_SRCS := $(ENGINE)/charset.c $(ENGINE)/water.c $(ENGINE)/chunk.c $(ENGINE)/tesselator.c \
              $(ENGINE)/glx.c $(ENGINE)/font.c $(ENGINE)/texture.c $(ENGINE)/postprocess.c \
              $(ENGINE)/hud.c $(ENGINE)/network.c $(ENGINE)/ping.c $(ENGINE)/demo.c $(ENGINE)/list.c \
              $(ENGINE)/player.c $(ENGINE)/lighting.c $(ENGINE)/sky.c $(ENGINE)/damagenumbers.c \
              $(ENGINE)/pie_menu.c $(ENGINE)/pie_menu_ext.c $(ENGINE)/teamplay.c \
              $(ENGINE)/teamplay_draw.c $(ENGINE)/flashlight_ext.c $(ENGINE)/client_key.c $(ENGINE)/killfeed.c \
              $(ENGINE)/daytime_weather.c $(ENGINE)/glowing_blocks.c

# CMake normally injects these from the build configuration.
CHECK_DEFS := -DKYROSPADES_VERSION='"dev"' -DKYROSPADES_MAJOR=0 -DKYROSPADES_MINOR=0 \
              -DKYROSPADES_PATCH=0 -DGIT_COMMIT_HASH='"none"'

# glx.c has three build configurations in CMakeLists.txt (legacy client state,
# programmable shaders, and GLES3/core). All three are parsed here so a change
# that only compiles in one of them fails the check instead of the user's build.
CHECK_VARIANTS := "" "-DGLX_PROGRAMMABLE" "-DGLX_PROGRAMMABLE -DOPENGL_CORE"

check:
	@set -e; for f in $(CHECK_SRCS); do \
		echo "  CHECK $$f"; \
		$(CC) $(CFLAGS) $(CHECK_DEFS) -fsyntax-only $(INC) $$f; \
	done
	@set -e; for v in $(CHECK_VARIANTS); do \
		echo "  CHECK $(ENGINE)/glx.c [$$v]"; \
		$(CC) $(CFLAGS) $(CHECK_DEFS) $$v -fsyntax-only $(INC) $(ENGINE)/glx.c; \
	done
	@echo "all checked"

clean:
	rm -rf $(BUILD) bench

.PHONY: all run clean check
