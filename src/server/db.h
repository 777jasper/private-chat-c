#ifndef DB_H
#define DB_H

#include <stddef.h>

/* Opens the database file at path (creating it if needed) and makes sure the table exist. Returns 0 on success, -1 on failure. */
int db_init(const char *path);

/* Close the database. */
void db_close(void);

/* Adds a user. Returns 0 on success, 1 if the username is taken, -1 on any other error. */
int db_add_user(const char *username, const char *pw_hash);

/* Copies the user's stored hash into out. Returns 0 if found, 1 if there is no such user, -1 0n error. */
int db_get_hash(const char *username, char *out, size_t out_size);

#endif
