#include <stdio.h>
#include <sodium.h>     // libsodium
#include "sqlite3.h"    // SQLite

int main(void)
{
    /* ---------- Part 1: libsodium ---------- */

    // Must be called once before any other libsodium function.
    if (sodium_init() < 0) {
        fprintf(stderr, "libsodium failed to initialize\n");
        return 1;
    }

    // A buffer the size of an encryption key, filled with secure random bytes.
    unsigned char key[crypto_secretbox_KEYBYTES];
    randombytes_buf(key, sizeof key);

    // Print only the first 4 bytes, in hex, just to see that it worked.
    printf("First 4 key bytes: ");
    for (int i = 0; i < 4; i++) {
        printf("%02x", key[i]);
    }
    printf("\n");

    /* ---------- Part 2: SQLite ---------- */

    printf("SQLite version: %s\n", sqlite3_libversion());

    // Open a temporary database that lives only in RAM.
    sqlite3 *db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    sqlite3_close(db);

    printf("Everything works!\n");
    return 0;
}
