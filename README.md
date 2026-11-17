# private-chat-c

A private one-to-one chat application written in C: a multi-threaded TCP server and a command-line client. Messages are encrypted on the client with libsodium before they are sent, so the server only relays and stores ciphertext it cannot read. Users log in with passwords that are stored as Argon2id hashes, and the credentials themselves are sent encrypted. Each conversation's history is kept in SQLite and is visible only to the two people in it.

## Features

- Registration and login with unique usernames; passwords stored only as Argon2id hashes
- Credentials sent in a sealed box with a one-time server challenge (no plaintext passwords, no replay)
- One-to-one messaging, with many independent conversations per user
- End-to-end encrypted message bodies (`crypto_box`); the server cannot read them
- Offline delivery: messages sent to a user who is not connected are stored and delivered at their next login
- Persistent history (`/history name`), readable only by the two participants
- No "online users" list anywhere; the reply to a send is the same whether or not the recipient is online
- One thread per client, with per-connection send locks, timeouts and a connection cap
- Orderly shutdown on Ctrl+C or SIGTERM

## Requirements

- Linux, `gcc`, `make`, `pkg-config`
- libsodium 1.0.22 (system `libsodium-devel`, or a local build as below)
- SQLite 3.54 amalgamation (`sqlite3.c` and `sqlite3.h`, compiled into the server)
- Optional: `valgrind`, the `sqlite3` command-line tool

## Setup without root (local libraries)

Run these from the project folder.

    PROJECT=$(pwd)
    mkdir -p deps/src && cd deps/src
    curl -LO https://download.libsodium.org/libsodium/releases/libsodium-1.0.22-stable.tar.gz
    tar -xzf libsodium-1.0.22-stable.tar.gz
    cd libsodium-stable
    ./configure --prefix="$PROJECT/deps"
    make -j"$(nproc)" && make install
    cd "$PROJECT"

Download the SQLite amalgamation and unpack it (still from the project folder):

    mkdir -p deps/sqlite && cd deps/sqlite
    curl -LO https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
    unzip sqlite-amalgamation-3540100.zip
    cd "$PROJECT"

The zip is 2990966 bytes. Check its SHA3-256 with `openssl dgst -sha3-256 <file>`; it should be `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`. If `unzip` is missing, use `python3 -m zipfile -e sqlite-amalgamation-3540100.zip .`. To use a different version, change `SQLITE_DIR` in the `Makefile` to match the folder name.

`env.sh` points `pkg-config` and the run-time loader at `deps/`. It finds its own folder, so no editing is needed.. Run it in every new terminal:

    source ./env.sh

If libsodium is installed system-wide, `env.sh` is not needed.

## Build

    source ./env.sh
    make --warn-undefined-variables

This creates the `build/` folder and builds `build/server`, `build/client`, and two test tools. `make clean` removes your own object files and keeps the slow-to-build `sqlite3.o`; `make clean-all` removes everything.

## Run

Start the server from the project folder. It uses the relative path `data/chat.db`.

    source ./env.sh
    ./build/server

It listens on `127.0.0.1:5555` only. On first start, it creates the `data/` folder (mode 700) and, inside it:

- `data/chat.db`: the SQLite database (users, public keys, encrypted messages)
- `data/server.key`: the server's secret key (mode 600; never share or commit it)
- `data/server.pub`: the server's public key

Each client machine needs a copy of the server's public key. Copy it once, by hand:

    mkdir -p ~/.chatapp
    cp data/server.pub ~/.chatapp/server.pub

The client always reads `~/.chatapp/server.pub`. If you run more than one copy of the server on the same machine (for example to test a fresh checkout), each copy has its own key, so start that client with a different home folder, for example `HOME=$HOME/fresh-home ./build/client`, and copy that server's `data/server.pub` into `fresh-home/.chatapp/`. Otherwise the copy overwrites the key the other server needs, and logins fail with `invalid username or password`.

The client does not fetch this key over the network, because a man in the middle could substitute their own.

Start a client:

    source ./env.sh
    ./build/client

It asks for `register` or `login`, then a username and a password (the password is not echoed). On first login it creates your keypair in `~/.chatapp/<username>.key` and uploads the public key.

### Client commands

| Input | Meaning |
|---|---|
| `/key name` | Fetch and cache a user's public key (needed before you can message them) |
| `name message...` | Send an encrypted message to `name` |
| `/history name` | Show the newest 50 messages of your conversation with `name` |
| `/quit` | Leave (Ctrl+D also works) |

Usernames are 3 to 32 characters: letters, digits and `_`. Passwords are 8 to 128 characters.

Back up `~/.chatapp/<username>.key`. If it is lost, old messages can no longer be decrypted.

## Protocol

Every message is a frame: a 5-byte header followed by the payload.

    +------------------+-----------+------------------------+
    | length (4 bytes) | type (1)  | payload (length bytes) |
    +------------------+-----------+------------------------+

The length is in network byte order and may not exceed 65536. Readers loop until all bytes arrive (`recv_all`), so partial reads and several frames in one TCP segment are handled.

| Type | Name | Direction | Payload |
|---|---|---|---|
| 3 | OK | server to client | text |
| 4 | ERROR | server to client | text |
| 6 | SEND | client to server | `recipient 00 body` |
| 7 | DELIVER | server to client | `sender 00 body` |
| 8 | SET_KEY | client to server | 32-byte public key |
| 9 | GET_KEY | client to server | username |
| 10 | KEY | server to client | `username 00 key(32)` |
| 11 | HISTORY_REQ | client to server | peer username |
| 12 | HISTORY_ITEM | server to client | `peer 00 from_me(1) sent_at(8) body` |
| 13 | HISTORY_END | server to client | peer username |
| 14 | CHALLENGE_REQ | client to server | empty |
| 15 | CHALLENGE | server to client | 32 random bytes |
| 16 | LOGIN_SEALED | client to server | sealed box (see below) |
| 17 | REGISTER_SEALED | client to server | sealed box (see below) |

Types 1, 2 (plaintext register and login) and 5 are unused. The server refuses plaintext register and login, so the encryption cannot be bypassed.

## Security design

**Passwords.** Stored as Argon2id strings (`crypto_pwhash_str`, interactive limits, per-user random salt). Login for a missing user runs a dummy verification, so the reply time does not reveal which usernames exist.

**Login on the wire.** The client requests a challenge (32 random bytes, per connection, usable once), then sends `crypto_box_seal(challenge | username 00 | password 00)` to the server's public key. A recorded login is useless on any other connection.

**Messages.** The sender encrypts with `crypto_box` (recipient's public key, sender's secret key, fresh random nonce per message) and sends `nonce | ciphertext`. The server stores and relays these bytes without interpreting them. Sender and recipient can both decrypt, because the shared secret is the same in both directions. A modified message fails its MAC check.

**Privacy.** The sender's name always comes from the server's login session, never from the request, so messages cannot be forged. History queries always use the logged-in name as one side, so a user can only read their own conversations. Key lookups give the same answer for a missing user and for a user without a key.

## Server design

- One detached thread per client; a counter caps connections at 128.
- `conn_t` holds the socket, a send lock (so frames from two threads never interleave), an `alive` flag and a reference count. A failed send shuts the connection down, because a half-sent frame cannot be repaired.
- A registry maps logged-in usernames to connections (at most 64, one connection per user), protected by a mutex.
- SQLite is shared through one connection in serialized mode. All SQL uses prepared statements.
- Timeouts: send 5 s per call and 10 s per frame; 10 s to finish a frame that was started; 30 s to log in after connecting. Idle logged-in users are not disconnected.
- On SIGINT or SIGTERM the server stops accepting, waits up to 20 s for client threads to leave, then closes the database.

## Testing

- `src/test_msgcrypt.c` is the encryption unit test. It checks that the recipient can decrypt, an outsider cannot, and tampering is rejected:

        gcc -Wall -Wextra -g src/test_msgcrypt.c src/client/msgcrypt.c -Isrc/client \
            $(pkg-config --cflags --libs libsodium) -o build/test_msgcrypt
        ./build/test_msgcrypt

- `scripts/stress.sh` runs 12 listeners and 12 senders at once and checks that each listener receives exactly its own message. Start the server in another terminal first. Useful settings: `SKIP_REGISTER=1`, `LOGIN_WAIT=60`, `DELIVER_WAIT=30`.

        bash scripts/stress.sh

  Afterwards remove the fake accounts `l01` to `l12` and `s01` to `s12` from the database.

- The whole server was run under `valgrind --leak-check=full` with the stress test: no leaks and no errors.

Scratch tools (not part of the application): `test_register_client` and `test_chat_client` are command-line tools used by the stress script. They log in with the sealed protocol but send message bodies unencrypted, so use them with throwaway accounts only. `test_libs.c` checks that libsodium and SQLite compile and link. `test_echo_server.c` and `test_echo_client.c` were the first framing test; the echo server uses the same port as the real server.

## Limitations

- After login, frames are not on an encrypted channel. Message bodies and credentials are protected, but an attacker in the middle could drop or inject frames.
- Users do not verify each other's keys. A malicious server could hand out a fake public key for someone.
- The server sees metadata: who writes to whom, and when.
- Registration reveals whether a username is taken.
- A message sent at the same instant as the recipient's login can be shown twice or slightly out of order, but not lost.
- A client holds at most 32 messages while waiting for a sender's key; more are dropped from the live view (they stay in the database). `/history` shows the newest 50.
- Listening is on localhost only, with no TLS.
- Replacing a user's key (for example logging in from a second machine) makes older messages unreadable there.

## Project layout

    src/common/   protocol.h, net_io.c/.h (framing, send_all/recv_all)
    src/server/   server.c, db.c, auth.c, conn.c, registry.c
    src/client/   client.c, keys.c, msgcrypt.c, sealedauth.c
    scripts/      stress.sh
    deps/         local libsodium and SQLite (not in git)
    data/         database and server keys (not in git)
    build/        compiled output (not in git)
    tmp/          scratch output (not in git)
