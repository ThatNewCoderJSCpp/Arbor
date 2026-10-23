#!/usr/bin/env bash
set -euo pipefail

abis="${1:-arm64-v8a,x86_64}"
out_dir="${2:-.}"
ndk="${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_HOME:-}}"
if [ -z "$ndk" ] || [ ! -f "$ndk/build/cmake/android.toolchain.cmake" ]; then
    echo "::error::set ANDROID_NDK_HOME to the Android NDK"
    exit 1
fi

root=$(pwd)
libs="$root/build-android-libs"
kit="$root/build-android-kit"
jobs="${JOBS:-4}"

cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DFIZMO_TUNE=OFF \
    -DARBOR_BUILD_TESTS=OFF -DARBOR_BUNDLE=OFF -DARBOR_WITH_WALNUT=ON
cmake --build build-host --target grove walnut -j "$jobs"
grove="$root/build-host/bin/grove"

rm -rf "$kit"
mkdir -p "$kit"
for abi in ${abis//,/ }; do
    cmake -S . -B "build-android-$abi" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
        -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
        -DFIZMO_TUNE=OFF -DFIZMO_BUILD_SHARED=OFF -DFIZMO_BUILD_SHADER_TOOL=OFF \
        -DARBOR_ANDROID_LIBS_DIR="$libs"
    cmake --build "build-android-$abi" --target arbor_android grove_android_kit -j "$jobs"
    test -f "$libs/$abi/libarbor_android.so"
    cp -r "build-android-$abi/kit/lib/grove/android/$abi" "$kit/$abi"
done

version=$(sed -n 's/^project(arbor VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -n 1)
IFS=. read -r major minor patch <<< "$version"
code=$(( ${major:-0} * 1000000 + ${minor:-0} * 1000 + ${patch:-0} ))

sign=()
if [ -n "${ARBOR_KEYSTORE_BASE64:-}" ]; then
    printf '%s' "$ARBOR_KEYSTORE_BASE64" | base64 -d > "${RUNNER_TEMP:-/tmp}/arbor.keystore"
    sign=(--keystore "${RUNNER_TEMP:-/tmp}/arbor.keystore" --ks-pass env:ARBOR_KEYSTORE_PASS)
    if [ -n "${ARBOR_KEY_ALIAS:-}" ]; then sign+=(--key-alias "$ARBOR_KEY_ALIAS"); fi
else
    echo "::warning::ARBOR_KEYSTORE_BASE64 isn't set, so the APK is signed with a debug key; a phone with an older Arbor APK signed by another key has to uninstall it first"
fi

mkdir -p "$out_dir"
apk="$out_dir/Arbor-$version-android.apk"
"$grove" build UI/arbor.grove --target android \
    --runtime-lib "$libs" --lib-name arbor_android --android-kit "$kit" --ndk "$ndk" --abi "$abis" \
    --set package=io.github.thatnewcoderjscpp.arbor \
    --set label=Arbor \
    --set "version=$version" \
    --set "version-code=$code" \
    "${sign[@]}" \
    -o "$apk"
ls -la "$apk"
