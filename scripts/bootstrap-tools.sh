#!/bin/sh
# User-local cross compiler for Debian 13 x86_64; no sudo or system changes.
# Debian verifies repository signatures and .deb hashes. TLS stays enabled.
set -eu
TOOLS=${TUBELIVIANO_TOOLS_DIR:-/workspace/toolchain}
mkdir -p "$TOOLS/apt/lists/partial" "$TOOLS/apt/cache/archives/partial" "$TOOLS/debs" "$TOOLS/root" "$TOOLS/apt/empty"
TOOLS=$(cd "$TOOLS" && pwd)
cat > "$TOOLS/apt/apt.conf" <<APT
Dir::Etc::main "/dev/null";
Dir::Etc::parts "$TOOLS/apt/empty";
Dir::Etc::sourcelist "$TOOLS/apt/sources.list";
Dir::Etc::sourceparts "$TOOLS/apt/empty";
Dir::State::Lists "$TOOLS/apt/lists";
Dir::Cache "$TOOLS/apt/cache";
APT
cat > "$TOOLS/apt/sources.list" <<'APT'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
APT
export APT_CONFIG="$TOOLS/apt/apt.conf"
apt-get update
(
    cd "$TOOLS/debs"
    apt-get download \
        gcc-mingw-w64-i686-win32=14.2.0-19+27+b1 \
        gcc-mingw-w64-i686-win32-runtime=14.2.0-19+27+b1 \
        gcc-mingw-w64-base=14.2.0-19+27+b1 \
        binutils-mingw-w64-i686=2.44-3+12+b1 \
        mingw-w64-i686-dev=12.0.0-5 \
        mingw-w64-common=12.0.0-5
    for package in gcc-mingw-w64*.deb binutils-mingw-w64-i686*.deb mingw-w64*.deb; do
        dpkg-deb -x "$package" "$TOOLS/root"
    done
)
ln -sf i686-w64-mingw32-gcc-win32 "$TOOLS/root/usr/bin/i686-w64-mingw32-gcc"
cat > "$TOOLS/env.sh" <<ENV
# Source from a Linux shell before make.
export PATH='$TOOLS/root/usr/bin':\$PATH
export CC='$TOOLS/root/usr/bin/i686-w64-mingw32-gcc'
export WINDRES='$TOOLS/root/usr/bin/i686-w64-mingw32-windres'
ENV
"$TOOLS/root/usr/bin/i686-w64-mingw32-gcc" --version
printf '\nCompiler prepared. Run: . "%s/env.sh"\n' "$TOOLS"
