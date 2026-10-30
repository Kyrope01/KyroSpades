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
# -Wno-type-limits: player.h's `local_player_id` is an unsigned char and
# PLAYERS_MAX is 256, so the "id in range" guards are provably dead. They are
# kept as defensive checks; the compiler is right that they never fire.
CFLAGS  += -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-type-limits

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
CHECK_SRCS := $(ENGINE)/water.c $(ENGINE)/chunk.c $(ENGINE)/tesselator.c \
              $(ENGINE)/glx.c $(ENGINE)/font.c $(ENGINE)/texture.c $(ENGINE)/postprocess.c \
              $(ENGINE)/hud.c $(ENGINE)/network.c $(ENGINE)/ping.c $(ENGINE)/demo.c $(ENGINE)/list.c

# CMake normally injects these from the build configuration.
CHECK_DEFS := -DKYROSPADES_VERSION='"dev"' -DKYROSPADES_MAJOR=0 -DKYROSPADES_MINOR=0 \
              -DKYROSPADES_PATCH=0 -DGIT_COMMIT_HASH='"none"' 

check:
	@set -e; for f in $(CHECK_SRCS); do \
		echo "  CHECK $$f"; \
		$(CC) $(CFLAGS) $(CHECK_DEFS) -fsyntax-only $(INC) $$f; \
	done
	@echo "all checked"

clean:
	rm -rf $(BUILD) bench

.PHONY: all run clean check
