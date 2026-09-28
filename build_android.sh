#!/usr/bin/env bash
# =============================================================================
#  build_android.sh — Cross-compile KyroSpades for Android via NDK + CMake
# =============================================================================
#
#  USAGE:
#    chmod +x build_android.sh
#    ./build_android.sh [OPTIONS]
#
#  OPTIONS:
#    --abi <ABI>           Target ABI. Default: arm64-v8a
#                          Choices: arm64-v8a | armeabi-v7a | x86_64 | x86
#    --api <LEVEL>         Android API level. Default: 21 (Android 5.0 — widest compatibility)
#    --build-type <TYPE>   CMake build type. Default: Release
#                          Choices: Release | Debug
#    --sound / --no-sound  Enable/disable OpenAL sound. Default: ON
#    --jobs <N>            Parallel build jobs. Default: nproc
#    --ndk <PATH>          Override NDK path (also reads $ANDROID_NDK_HOME)
#    --help                Show this message
#
#  PREREQUISITES (macOS):
#    brew install cmake git ninja
#    # Install Android NDK: https://developer.android.com/ndk/downloads
#    # Or via Android Studio → SDK Manager → NDK (Side by side)
#    export ANDROID_NDK_HOME=~/Library/Android/sdk/ndk/<version>
#
#  PREREQUISITES (Linux):
#    sudo apt install cmake git ninja-build
#    # Install Android NDK: https://developer.android.com/ndk/downloads
#    export ANDROID_NDK_HOME=~/Android/Sdk/ndk/<version>
#
# =============================================================================

set -euo pipefail

# ── Defaults ─────────────────────────────────────────────────────────────────
ABI="arm64-v8a"
API_LEVEL="21"
BUILD_TYPE="Release"
ENABLE_SOUND="ON"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
NDK_PATH="${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}"

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
die()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case $1 in
        --abi)         ABI="$2";        shift 2 ;;
        --api)         API_LEVEL="$2";  shift 2 ;;
        --build-type)  BUILD_TYPE="$2"; shift 2 ;;
        --sound)       ENABLE_SOUND="ON"; shift ;;
        --no-sound)    ENABLE_SOUND="OFF"; shift ;;
        --jobs)        JOBS="$2";       shift 2 ;;
        --ndk)         NDK_PATH="$2";   shift 2 ;;
        --help)
            sed -n '/^#  USAGE/,/^# ====/p' "$0" | head -n -1
            exit 0
            ;;
        *) die "Unknown option: $1  (use --help)" ;;
    esac
done

# ── Validate ABI ─────────────────────────────────────────────────────────────
case "$ABI" in
    arm64-v8a|armeabi-v7a|x86_64|x86) ;;
    *) die "Unsupported ABI: $ABI  (arm64-v8a | armeabi-v7a | x86_64 | x86)" ;;
esac

# ── Locate NDK ────────────────────────────────────────────────────────────────
# Resolves the toolchain even when the NDK path has a letter suffix (r27b, r27c …)
# or when the user pointed at a parent directory instead of the NDK root.
resolve_ndk() {
    local base="$1"
    # 1. Exact path first
    [[ -f "$base/build/cmake/android.toolchain.cmake" ]] && { echo "$base"; return 0; }
    # 2. The user gave a parent dir — find the newest NDK inside it
    local best
    best=$(find "$base" -maxdepth 2 -name "android.toolchain.cmake" 2>/dev/null \
           | sed 's|/build/cmake/android.toolchain.cmake||' \
           | sort -V | tail -1)
    [[ -n "$best" ]] && { echo "$best"; return 0; }
    # 3. Glob for letter-suffixed siblings  (e.g. android-ndk-r27  →  android-ndk-r27c)
    local parent; parent="$(dirname "$base")"
    local stem;   stem="$(basename "$base")"
    best=$(find "$parent" -maxdepth 1 -type d -name "${stem}*" 2>/dev/null \
           | while read -r d; do
               [[ -f "$d/build/cmake/android.toolchain.cmake" ]] && echo "$d"
             done \
           | sort -V | tail -1)
    [[ -n "$best" ]] && { echo "$best"; return 0; }
    return 1
}

if [[ -n "$NDK_PATH" ]]; then
    # User supplied a path — resolve it, give a friendly error if still wrong
    RESOLVED=$(resolve_ndk "$NDK_PATH") || {
        PARENT=$(dirname "$NDK_PATH")
        FOUND=$(find "$PARENT" -maxdepth 2 -name "android.toolchain.cmake" 2>/dev/null | head -5)
        echo -e "${RED}[ERROR]${NC} Toolchain not found under: $NDK_PATH" >&2
        if [[ -n "$FOUND" ]]; then
            echo -e "${YELLOW}[HINT]${NC}  Found toolchains nearby — try one of these paths:" >&2
            echo "$FOUND" | sed 's|/build/cmake/android.toolchain.cmake||' | while read -r p; do
                echo "          --ndk $p" >&2
            done
        else
            echo -e "${YELLOW}[HINT]${NC}  NDK zips extract with a letter suffix, e.g. android-ndk-r27c." >&2
            echo "         Run:  ls $(dirname "$NDK_PATH")" >&2
            echo "         Then: ./build_android.sh --ndk /path/to/android-ndk-r27c" >&2
        fi
        exit 1
    }
    NDK_PATH="$RESOLVED"
else
    # Auto-detect from common install locations
    for candidate in \
        "$HOME/Library/Android/sdk/ndk" \
        "$HOME/Android/Sdk/ndk" \
        "/opt/android-ndk" \
        "/usr/local/android-ndk" \
        "$HOME/android-ndk" \
        "$HOME/android"
    do
        RESOLVED=$(resolve_ndk "$candidate" 2>/dev/null) && { NDK_PATH="$RESOLVED"; break; }
    done
fi

if [[ -z "$NDK_PATH" ]]; then
    die "Android NDK not found. Fix:\n\n" \
        "  1. Download NDK r27: https://developer.android.com/ndk/downloads\n" \
        "     Unzip it — it extracts to a folder like  android-ndk-r27c\n\n" \
        "  2. Either export ANDROID_NDK_HOME=/path/to/android-ndk-r27c\n" \
        "     Or pass:          ./build_android.sh --ndk /path/to/android-ndk-r27c"
fi

TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake"
ok "NDK found at: $NDK_PATH"

# ── Tool checks ───────────────────────────────────────────────────────────────
for tool in cmake git ninja; do
    command -v "$tool" &>/dev/null || die "'$tool' not found. Install it first."
done
CMAKE_VERSION=$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+')
CMAKE_MAJOR=$(echo "$CMAKE_VERSION" | cut -d. -f1)
CMAKE_MINOR=$(echo "$CMAKE_VERSION" | cut -d. -f2)
if (( CMAKE_MAJOR < 3 )) || { (( CMAKE_MAJOR == 3 )) && (( CMAKE_MINOR < 14 )); }; then
    die "CMake >= 3.14 required. Found $CMAKE_VERSION"
fi

# ── Directories ───────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"   # Assumes script lives in the project root
BUILD_ROOT="$PROJECT_DIR/build-android"
DEPS_BUILD="$BUILD_ROOT/deps-build"
DEPS_INSTALL="$BUILD_ROOT/deps-install"
FINAL_BUILD="$BUILD_ROOT/KyroSpades-${ABI}"

mkdir -p "$DEPS_BUILD" "$DEPS_INSTALL" "$FINAL_BUILD"

info "Configuration:"
info "  ABI:        $ABI"
info "  API level:  android-$API_LEVEL  (minimum: 21 = Android 5.0)"
info "  Build type: $BUILD_TYPE"
info "  Sound:      $ENABLE_SOUND"
info "  Jobs:       $JOBS"
echo

# ── Common CMake cross-compile flags ─────────────────────────────────────────
COMMON_CMAKE_FLAGS=(
    "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE"
    "-DANDROID_ABI=$ABI"
    "-DANDROID_PLATFORM=android-$API_LEVEL"
    "-DANDROID_STL=c++_static"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DCMAKE_INSTALL_PREFIX=$DEPS_INSTALL"
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    "-GNinja"
)

# ── Helper: cmake configure + build + install ─────────────────────────────────
build_dep() {
    local name="$1"; shift
    local src="$1";  shift
    local build_dir="$DEPS_BUILD/$name"
    info "Building dependency: $name"
    mkdir -p "$build_dir"
    cmake -S "$src" -B "$build_dir" "${COMMON_CMAKE_FLAGS[@]}" "$@" -DCMAKE_INSTALL_PREFIX="$DEPS_INSTALL"
    cmake --build "$build_dir" --parallel "$JOBS"
    cmake --install "$build_dir"
    ok "$name built and installed."
}

# ── Clone helper ──────────────────────────────────────────────────────────────
# NOTE: all logging goes to stderr so the captured return value is only the path
clone_or_update() {
    local name="$1"; local url="$2"; local tag="$3"
    local dir="$DEPS_BUILD/src/$name"
    if [[ -d "$dir/.git" ]]; then
        info "$name source already present, skipping clone." >&2
    else
        info "Cloning $name @ $tag ..." >&2
        git clone --depth 1 --branch "$tag" "$url" "$dir" >&2
    fi
    echo "$dir"
}

# ── 1. libdeflate ─────────────────────────────────────────────────────────────
LIBDEFLATE_SRC=$(clone_or_update libdeflate \
    https://github.com/ebiggers/libdeflate.git \
    v1.20)
build_dep libdeflate "$LIBDEFLATE_SRC" \
    -DLIBDEFLATE_BUILD_SHARED_LIB=OFF \
    -DLIBDEFLATE_BUILD_GZIP=OFF \
    -DLIBDEFLATE_BUILD_TESTS=OFF

# ── 2. enet ───────────────────────────────────────────────────────────────────
ENET_SRC=$(clone_or_update enet \
    https://github.com/lsalzman/enet.git \
    v1.3.18)

# enet doesn't ship a modern CMakeLists; write a minimal one if absent
if [[ ! -f "$ENET_SRC/CMakeLists.txt" ]]; then
    warn "enet has no CMakeLists.txt — generating a minimal one."
    cat > "$ENET_SRC/CMakeLists.txt" <<'ENET_CMAKE'
cmake_minimum_required(VERSION 3.14)
project(enet C)
file(GLOB ENET_SOURCES *.c)
add_library(enet STATIC ${ENET_SOURCES})
target_include_directories(enet PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                                       $<INSTALL_INTERFACE:include>)
install(TARGETS enet EXPORT enet-targets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT enet-targets
    FILE enet-config.cmake
    NAMESPACE enet::
    DESTINATION lib/cmake/enet)
ENET_CMAKE
fi

build_dep enet "$ENET_SRC" \
    -DCMAKE_INSTALL_LIBDIR=lib

# enet's CMakeLists hardcodes lib/static/ on some versions regardless of
# CMAKE_INSTALL_LIBDIR. Move it to the flat lib/ that Findenet.cmake expects.
if [[ -f "$DEPS_INSTALL/lib/static/libenet.a" && ! -f "$DEPS_INSTALL/lib/libenet.a" ]]; then
    mv "$DEPS_INSTALL/lib/static/libenet.a" "$DEPS_INSTALL/lib/libenet.a"
    info "Moved libenet.a from lib/static/ to lib/"
fi

# ── 3. OpenAL-Soft (optional) ─────────────────────────────────────────────────
if [[ "$ENABLE_SOUND" == "ON" ]]; then
    OPENAL_SRC=$(clone_or_update openal-soft \
        https://github.com/kcat/openal-soft.git \
        1.23.1)
    build_dep openal-soft "$OPENAL_SRC" \
        -DLIBTYPE=STATIC \
        -DALSOFT_EXAMPLES=OFF \
        -DALSOFT_TESTS=OFF \
        -DALSOFT_UTILS=OFF \
        -DALSOFT_BACKEND_OPENSL=ON \
        -DALSOFT_BACKEND_OBOE=OFF
fi

# ── Android OpenGL ES / EGL shim ─────────────────────────────────────────────
# CMake's built-in FindOpenGL.cmake looks for desktop libOpenGL.so which doesn't
# exist on Android.  We drop a replacement into a temp dir and prepend it to
# CMAKE_MODULE_PATH so it takes priority over the built-in.
OPENGL_OVERRIDE_DIR="$BUILD_ROOT/cmake-overrides"
mkdir -p "$OPENGL_OVERRIDE_DIR"
cat > "$OPENGL_OVERRIDE_DIR/FindOpenGL.cmake" << 'FINDOPENGL_EOF'
# FindOpenGL.cmake — Android OpenGL ES / EGL shim
# Replaces CMake's built-in module when cross-compiling for Android.
set(OPENGL_FOUND          TRUE)
set(OpenGL_FOUND          TRUE)
set(OpenGL_OpenGL_FOUND   TRUE)
set(OpenGL_EGL_FOUND      TRUE)
set(OPENGL_INCLUDE_DIR         "")
set(OPENGL_EGL_INCLUDE_DIRS    "")
set(OPENGL_LIBRARIES           "GLESv2")
set(OPENGL_opengl_LIBRARY      "GLESv2")
set(OPENGL_egl_LIBRARY         "EGL")

if(NOT TARGET OpenGL::OpenGL)
    add_library(OpenGL::OpenGL INTERFACE IMPORTED GLOBAL)
    set_target_properties(OpenGL::OpenGL PROPERTIES
        INTERFACE_LINK_LIBRARIES "GLESv2")
endif()
if(NOT TARGET OpenGL::EGL)
    add_library(OpenGL::EGL INTERFACE IMPORTED GLOBAL)
    set_target_properties(OpenGL::EGL PROPERTIES
        INTERFACE_LINK_LIBRARIES "EGL")
endif()
if(NOT TARGET OpenGL::GL)
    add_library(OpenGL::GL INTERFACE IMPORTED GLOBAL)
    set_target_properties(OpenGL::GL PROPERTIES
        INTERFACE_LINK_LIBRARIES "GLESv2")
endif()
FINDOPENGL_EOF

info "Android OpenGL ES shim written to $OPENGL_OVERRIDE_DIR/FindOpenGL.cmake"

# ── 4. KyroSpades ─────────────────────────────────────────────────────────────

# Wipe any stale CMake cache from a previous failed configure so that
# find_package variables like enet_LIBRARY don't stay stuck at NOTFOUND.
if [[ -f "$FINAL_BUILD/CMakeCache.txt" ]]; then
    info "Clearing stale CMake cache in $FINAL_BUILD ..."
    rm -f "$FINAL_BUILD/CMakeCache.txt"
    rm -rf "$FINAL_BUILD/CMakeFiles"
fi

# SDL2 is built via FetchContent; its own cmake cache lives inside _deps/.
# If SDL_HIDAPI was ON in a previous build we must wipe it so the flag takes effect.
SDL2_BUILD_CACHE="$FINAL_BUILD/_deps/sdl2-build/CMakeCache.txt"
if [[ -f "$SDL2_BUILD_CACHE" ]] && grep -q "SDL_HIDAPI:BOOL=ON" "$SDL2_BUILD_CACHE" 2>/dev/null; then
    info "Wiping stale SDL2 build cache (SDL_HIDAPI was ON) ..."
    rm -f "$SDL2_BUILD_CACHE"
    rm -rf "$FINAL_BUILD/_deps/sdl2-build/CMakeFiles"
fi

# Locate the enet library — handle both flat lib/ and lib/static/ installs.
ENET_LIB=""
for candidate in \
    "$DEPS_INSTALL/lib/libenet.a" \
    "$DEPS_INSTALL/lib/static/libenet.a"
do
    if [[ -f "$candidate" ]]; then
        ENET_LIB="$candidate"
        break
    fi
done
[[ -z "$ENET_LIB" ]] && die "libenet.a not found under $DEPS_INSTALL — enet build may have failed."
info "enet library: $ENET_LIB"

DEFLATE_LIB="$DEPS_INSTALL/lib/libdeflate.a"
[[ -f "$DEFLATE_LIB" ]] || die "libdeflate.a not found at $DEFLATE_LIB"

info "Configuring KyroSpades..."

cmake -S "$PROJECT_DIR" -B "$FINAL_BUILD" \
    "${COMMON_CMAKE_FLAGS[@]}" \
    "-DCMAKE_PREFIX_PATH=$DEPS_INSTALL" \
    "-DCMAKE_MODULE_PATH=$OPENGL_OVERRIDE_DIR;$PROJECT_DIR/cmake/Modules" \
    -DENABLE_SDL=ON \
    -DENABLE_GLFW=OFF \
    -DENABLE_OPENGLES=ON \
    -DENABLE_TOUCH=ON \
    -DENABLE_ANDROID_FILE=ON \
    "-DENABLE_SOUND=$ENABLE_SOUND" \
    -DENABLE_RPC=OFF \
    -DFETCHCONTENT_QUIET=OFF \
    -DSDL_HIDAPI=OFF \
    -DSDL_HIDAPI_JOYSTICK=OFF \
    "-DCMAKE_EXE_LINKER_FLAGS=-lc++ -lc++abi" \
    "-Denet_LIBRARY=$ENET_LIB" \
    "-Denet_INCLUDE_DIR=$DEPS_INSTALL/include" \
    "-Ddeflate_LIBRARY=$DEFLATE_LIB" \
    "-Ddeflate_INCLUDE_DIR=$DEPS_INSTALL/include"

info "Compiling KyroSpades..."

# Platform fixes live in the sources (common.h, http.h, window.c and
# cameracontroller.c); never rewrite tracked files during a build.
cmake --build "$FINAL_BUILD" --parallel "$JOBS"

echo
ok "============================================================"
ok " Build complete!"
ok " Output binary: $FINAL_BUILD/KyroSpades/client"
ok "============================================================"
echo
warn "NOTE: This produces a native binary, NOT an installable APK."
warn "To package as an APK you need an Android Studio project that"
warn "wraps SDL2's Java activity. See ANDROID_BUILD_GUIDE.md."
