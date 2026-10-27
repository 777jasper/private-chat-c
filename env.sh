# env.sh - our "activate" script for the local libraries

CHATAPP_DEPS=$HOME/Documents/TCP/chatapp/deps

# TODO 1: tell pkg-config where libsodium.pc lives.
# Look at your last ls output: which folder inside $CHATAPP_DEPS holds libsodium.pc?
export PKG_CONFIG_PATH=$CHATAPP_DEPS/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}

# TODO 2: tell Linux where libsodium.so lives at run time.
# Which folder inside $CHATAPP_DEPS holds libsodium.so?
export LD_LIBRARY_PATH=$CHATAPP_DEPS/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
