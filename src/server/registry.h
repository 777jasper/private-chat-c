#ifndef REGISTRY_H
#define REGISTRY_H

#define REGISTRY_MAX 64 /* most users that can be logged in at once. */
#define REGISTRY_NAME_SIZE 33 /* 32 characters plus the final 00. */

/* Records that username is logged in on socket fd.
   Returns 0 on success, 1 if that username is already logged in
   on another connection, -1 if the table is full. */
int registry_add(const char *username, int fd);

/* Removes whatever entry uses socket fd. Does nothing if there is none. */
void registry_remove(int fd);

#endif /* REGISTRY_H */
