#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# build_ios.sh — console-only iOS build for KyroSpades.
#
# Produces an UNSIGNED  build-ios/KyroSpades.app . Signing + install to a device
# is Phase 2 (sign_ios.sh / Sideloadly) because free-Apple-ID certs can't live
# in CI.
#
# Requirements (all CLI, no Xcode IDE):
#   - macOS with Xcode.app installed (the *Command Line Tools* package alone
#     does NOT ship the iPhoneOS SDK). Point at it once:
#         sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
#   - cmake >= 3.14, git, and ideally ninja (brew install cmake ninja)
#
# Usage:
#   ./build_ios.sh                 # device build (arm64, iphoneos)
#   IOS_SYSROOT=iphonesimulator ./build_ios.sh   # simulator build
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build-ios"
PREFIX="$BUILD/prefix"
SRC="$BUILD/deps-src"
TOOLCHAIN="$ROOT/ios/ios.toolchain.cmake"

IOS_SYSROOT="${IOS_SYSROOT:-iphoneos}"
IOS_ARCH="${IOS_ARCH:-arm64}"
IOS_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-13.0}"

# Pinned to match the desktop/Android builds where they overlap.
SDL2_TAG="release-2.30.10"
OPENAL_TAG="1.23.1"
ENET_TAG="v1.3.18"
LIBDEFLATE_TAG="v1.20"

GEN="Unix Makefiles"
command -v ninja >/dev/null 2>&1 && GEN="Ninja"

mkdir -p "$BUILD" "$PREFIX" "$SRC"

TC_ARGS=(
	-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
	-DIOS_SYSROOT="$IOS_SYSROOT"
	-DIOS_ARCH="$IOS_ARCH"
	-DIOS_DEPLOYMENT_TARGET="$IOS_DEPLOYMENT_TARGET"
	-DCMAKE_INSTALL_PREFIX="$PREFIX"
	-DCMAKE_PREFIX_PATH="$PREFIX"
	-DCMAKE_BUILD_TYPE=Release
	# CMake 4.x rejects the ancient cmake_minimum_required() in enet/cglm/vxl.
	# This restores < 3.5 policy compatibility for every dep + the app build.
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

clone() { # repo tag dir
	local repo="$1" tag="$2" dir="$3"
	if [ ! -d "$SRC/$dir" ]; then
		echo ">> cloning $dir ($tag)"
		git clone --depth 1 --branch "$tag" "$repo" "$SRC/$dir"
	fi
}

build_dep() { # dir [extra cmake args...]
	local dir="$1"; shift
	echo ">> building $dir for $IOS_SYSROOT/$IOS_ARCH"
	cmake -S "$SRC/$dir" -B "$BUILD/$dir" -G "$GEN" "${TC_ARGS[@]}" "$@"
	cmake --build "$BUILD/$dir" --config Release
	cmake --install "$BUILD/$dir" --config Release
}

# ── Dependencies that the app finds via find_package() ────────────────────────
# (cglm is pulled + built by the app's own FetchContent, and libvxl is vendored
#  in deps/libvxl and compiled as part of the app, so neither is prebuilt here.)

clone https://github.com/libsdl-org/SDL          "$SDL2_TAG"       SDL
build_dep SDL -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF

clone https://github.com/kcat/openal-soft         "$OPENAL_TAG"     openal-soft
build_dep openal-soft \
	-DLIBTYPE=STATIC \
	-DALSOFT_BACKEND_COREAUDIO=ON \
	-DALSOFT_EXAMPLES=OFF -DALSOFT_UTILS=OFF -DALSOFT_TESTS=OFF \
	-DALSOFT_INSTALL=ON

clone https://github.com/lsalzman/enet            "$ENET_TAG"       enet
build_dep enet

clone https://github.com/ebiggers/libdeflate      "$LIBDEFLATE_TAG" libdeflate
build_dep libdeflate \
	-DLIBDEFLATE_BUILD_SHARED_LIB=OFF \
	-DLIBDEFLATE_BUILD_STATIC_LIB=ON \
	-DLIBDEFLATE_BUILD_GZIP=OFF

# ── The app itself ────────────────────────────────────────────────────────────
echo ">> configuring KyroSpades"
cmake -S "$ROOT" -B "$BUILD/app" -G "$GEN" "${TC_ARGS[@]}" \
	-DENABLE_SDL=ON -DENABLE_OPENGLES=ON -DENABLE_TOUCH=ON -DENABLE_SOUND=ON \
	-DOPENAL_INCLUDE_DIR="$PREFIX/include" \
	-DOPENAL_LIBRARY="$PREFIX/lib/libopenal.a" \
	-Denet_INCLUDE_DIR="$PREFIX/include" \
	-Denet_LIBRARY="$PREFIX/lib/static/libenet.a" \
	-Ddeflate_INCLUDE_DIR="$PREFIX/include" \
	-Ddeflate_LIBRARY="$PREFIX/lib/libdeflate.a"

echo ">> building KyroSpades"
# Overlays can preserve archive mtimes older than an earlier object file.
# Clean the build outputs rather than touching tracked source timestamps:
# this ensures the staged overlay is recompiled on every bundle build.
cmake --build "$BUILD/app" --config Release --clean-first

# ── Assemble the (unsigned) .app bundle ───────────────────────────────────────
# The CMake target writes the executable + a full resource payload (resources/,
# extracted bsresources.zip, overrides) into <root>/build/KyroSpades via its
# RUNTIME_OUTPUT_DIRECTORY + POST_BUILD steps. We fold that into the bundle and
# drop in the Info.plist.
PAYLOAD="$ROOT/build/KyroSpades"
APP="$BUILD/KyroSpades.app"

if [ ! -x "$PAYLOAD/client" ]; then
	echo "!! expected executable at $PAYLOAD/client — build layout changed?" >&2
	exit 1
fi

rm -rf "$APP"
mkdir -p "$APP"
cp -R "$PAYLOAD/." "$APP/"
cp "$ROOT/ios/Info.plist" "$APP/Info.plist"
printf 'APPL????' > "$APP/PkgInfo"
# App icons, if generated (ios/make_icons.sh). Loose PNGs at the bundle root,
# referenced by CFBundleIcons in Info.plist. Skipped silently if absent.
if compgen -G "$ROOT/ios/icons/*.png" >/dev/null 2>&1; then
	cp "$ROOT/ios/icons/"*.png "$APP/"
fi
# logs/cache/etc are written to the sandbox at runtime; don't ship empty dirs.
rm -rf "$APP/logs" "$APP/cache" "$APP/screenshots" "$APP/demos" "$APP/vxl"

echo ""
echo "✅ Unsigned bundle ready: $APP"
echo "   Next (Phase 2): sign + install. Easiest path —"
echo "     • Sideloadly: drag $APP in, plug in phone, Start."
echo "     • Or CLI:     ./sign_ios.sh  (codesign + devicectl install)"
