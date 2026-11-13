#ifndef DB_H
#define DB_H

#include <stddef.h>
#include <stdint.h>

#define DB_PUBKEY_SIZE 32 /* size of a public key in bytes. */

/* Opens the database file at path (creating it if needed) and makes sure the table exist. Returns 0 on success, -1 on failure. */
int db_init(const char *path);

/* Close the database. */
void db_close(void);

/* Adds a user. Returns 0 on success, 1 if the username is taken, -1 on any other error. */
int db_add_user(const char *username, const char *pw_hash);

/* Copies the user's stored hash into out. Returns 0 if found, 1 if there is no such user, -1 on error. */
int db_get_hash(const char *username, char *out, size_t out_size);

/* Stores the user's public key (DB_PUBKEY_SIZE bytes). Return 0 on success, 1 if no such user, -1 on error. */
int db_set_pubkey(const char *username, const unsigned char *key);

/* Copies the user's public key (DB_PUBKEY_SIZE bytes) into out. Returns 0 if found, 1 if there is no such user or no key yet, -1 on error. */
int db_get_pubkey(const char *username, unsigned char *out);

/* Stores one encrypted message. Returns 0 on success, 1 if either user doesn't exist, -1 on error. id is written to *id_out (which may be NULL) on success */
int db_store_message(const char *sender, const char *recipient, const uint8_t *body, size_t body_len, int64_t *id_out);

/* Marks a stored message as delivered. Returns 0 success, -1 on error. */
int db_mark_delivered(int64_t id);

/* Fetches the oldest undelivered message for username. On success (0) *id is the message id, sender holds the sender's name, and *body is a malloc'd buffer of *body_len bytes that the caller must free. Returns 1 if nothing is waiting, -1 on error. */
int db_next_undelivered(const char *username, int64_t *id, char *sender, size_t sender_size, uint8_t **body, size_t *body_len);
#endif
