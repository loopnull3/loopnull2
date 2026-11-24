#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2026 Hokkaido contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the game hosts of the Banana Native release :banana embeds (src/android/banana/UPSTREAM.txt) with Hokkaido's
# patches to them (src/android/banana/native_patches/*.patch), straight with CMake and the NDK's toolchain file:
#   build_bananative_hosts.sh <work dir> <out dir>
# Then build Hokkaido with BANANA_NATIVE_LIBS=<out dir> (or -PbananaNativeLibs): libnxhost_<title ID>.so and libnxlaunch_<title ID>.so
# replace the release's, the packer's libraries stay the release's. Needs git, cmake, ninja, python3 and ANDROID_NDK_ROOT (the NDK the
# release names, android/app/build.gradle ndkVersion). Uses ccache when it is on the PATH.
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <work dir> <out dir>" >&2
    exit 2
fi
work=$(mkdir -p "$1" && cd "$1" && pwd)
out=$(mkdir -p "$2" && cd "$2" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
upstream="$root/src/android/banana/UPSTREAM.txt"
value() { sed -n "s/^$1 = //p" "$upstream"; }
repo=$(value repo)
commit=$(value commit)
ndk=${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT}

src="$work/src"
if [ ! -d "$src/.git" ]; then
    git init -q "$src"
    git -C "$src" remote add origin "$repo.git"
fi
git -C "$src" fetch -q --depth 1 origin "$commit"
git -C "$src" checkout -q --force FETCH_HEAD
git -C "$src" clean -q -fdx
cp -r "$root/src/android/banana/native_patches/astcenc" "$src/dm/third_party/astcenc"   # (the patches' HDR block conversion builds it)
for p in "$root"/src/android/banana/native_patches/*.patch; do
    [ -e "$p" ] || continue
    git -C "$src" apply --whitespace=nowarn "$p"
    echo "applied $(basename "$p")"
done

games=$(sed -n 's/^nxGames=//p' "$src/android/app/gradle.properties")
launcher=()
if command -v ccache > /dev/null; then
    launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache)
fi
# As android/app/build.gradle's externalNativeBuild (minSdk 33, LSFG in, 16 KB pages)
cmake -S "$src/android/app/src/main/cpp" -B "$work/build" -G Ninja "${launcher[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=33 \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNX_GAMES="$games" -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DNX_LSFG=ON
targets=()
for g in ${games//,/ }; do
    targets+=("nxhost_$g" "nxlaunch_$g")
done
cmake --build "$work/build" --parallel "$(nproc)" --target "${targets[@]}"

rm -f "$out"/*.so
find "$work/build" -name 'libnx*.so' -exec cp {} "$out"/ \;
echo "Built from $commit with $(ls "$root"/src/android/banana/native_patches/*.patch 2>/dev/null | wc -l) patches:"
ls "$out"
