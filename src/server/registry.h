#ifndef REGISTRY_H
#define REGISTRY_H

#include "conn.h"

#define REGISTRY_MAX 64 /* most users that can be logged in at once. */
#define REGISTRY_NAME_SIZE 33 /* 32 characters plus the final 00. */

/* Records that username is logged in on connection c and takes a reference on c.
   Returns 0 on success, 1 if that username is already logged in
   on another connection, -1 if the table is full. */
int registry_add(const char *username, conn_t *c);

/* Removes the entry for c (if any) and drops the registry reference. */
void registry_remove(conn_t *c);

/* Returns the connection logged in as username, with a reference added for the caller, or NULL if nobody is. The caller must call conn_unref when finished. */
conn_t *registry_lookup(const char *username);

#endif /* REGISTRY_H */
