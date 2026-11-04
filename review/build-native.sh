#!/bin/sh
# Build only in an isolated copy/capture. No live Web64 paths are accepted implicitly.
set -eu
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    echo 'usage: build-native.sh <verified-staged-vice-tree> <build-dir> [normal|profiling]' >&2
    exit 2
fi
source_tree=$(cd "$1" && pwd)
if [ ! -f "$source_tree/../manifest.json" ] || [ ! -f "$source_tree/../prepared.json" ] ||
   [ ! -f "$source_tree/src/arch/headless/x64-native-host-impl.h" ]; then
    echo 'verified x64-owned native stage required; run prepare-native.py first' >&2
    exit 2
fi
variant=${3:-normal}
case "$variant" in
    normal) profiler=0 ;;
    profiling) profiler=1 ;;
    *) echo 'variant must be normal or profiling' >&2; exit 2 ;;
esac
host_flags="-O2 -DVICE_WEB=1 -DWEB64_HOST_NATIVE=1 -DENABLE_PROFILER=$profiler"
mkdir -p "$2"
build_dir=$(cd "$2" && pwd)
case "$build_dir/" in "$source_tree/"*)
    echo 'out-of-tree build required' >&2
    exit 2
    ;;
esac
cd "$build_dir"
if [ -f .x64-source-tree ] && [ "$(cat .x64-source-tree)" != "$source_tree" ]; then
    echo 'build directory belongs to another source capture; use a new output' >&2
    exit 2
fi
if [ -f .x64-variant ] && [ "$(cat .x64-variant)" != "$variant" ]; then
    echo 'build directory belongs to another variant; use a new output' >&2
    exit 2
fi
if [ ! -f config.status ]; then
    sh "$source_tree/configure" \
        --enable-headlessui --with-fastsid \
        --disable-realdevice --disable-rs232 --disable-ethernet --disable-midi \
        --without-alsa --without-pulse --without-oss --without-sun \
        --without-portaudio --without-libcurl --without-png \
        --disable-html-docs --disable-pdf-docs --disable-openmp \
        --prefix="$build_dir/prefix" \
        CC=gcc CXX=g++ CFLAGS="$host_flags" CXXFLAGS="$host_flags"
fi
printf '%s\n' "$source_tree" > .x64-source-tree
printf '%s\n' "$variant" > .x64-variant
# These support archives enter LIBS outside x64sc_DEPENDENCIES in this fork.
make -C src/lib/linenoise-ng -j"${X64_BUILD_JOBS:-4}"
make -C src/lib/md5 -j"${X64_BUILD_JOBS:-4}"
# Reuse the fork's explicit archive-dependency closure; do not build unrelated machines.
rules="$source_tree/build/web/runtime-dependencies.mk"
make -C src -f Makefile -f "$rules" web64-runtime-dependencies \
    WEB64_RUNTIME_DEPENDENCY_RULES="$rules" -j"${X64_BUILD_JOBS:-4}"
exe_suffix=$(sed -n 's/^EXEEXT = //p' src/Makefile)
make -C src -f Makefile -f "$rules" "x64sc$exe_suffix" -j"${X64_BUILD_JOBS:-4}"
