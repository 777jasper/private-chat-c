# env.sh - source this file from bash:  source ./env.sh
# It finds its own folder, so the project can live anywhere.

CHATAPP_DEPS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/deps"

# Where pkg-config finds libsodium.pc (lib64 on openSUSE, lib on some systems)
export PKG_CONFIG_PATH="$CHATAPP_DEPS/lib64/pkgconfig:$CHATAPP_DEPS/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

# Where Linux finds libsodium.so when a program runs
export LD_LIBRARY_PATH="$CHATAPP_DEPS/lib64:$CHATAPP_DEPS/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
