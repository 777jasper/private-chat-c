#ifndef CONN_H
#define CONN_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

typedef struct conn{
	int fd;
	pthread_mutex_t send_lock; /* held while writing a frame; also guards alive */
	int alive; /* 1 = socket open, 0 = closed */
	atomic_int refs; /* number of holders of this pointer */
}conn_t;

/* Allocates a conn for socket fd, with refs = 1 (the caller's reference). Returns NULL on failure */
conn_t *conn_create(int fd);

/* Sends one frame, holding the send lock. Returns 0 on success, -1 on failure or if the connection was already shutdown */
int conn_send(conn_t *c, uint8_t type, const uint8_t *payload, uint32_t len);

/* Adds a reference */
void conn_ref(conn_t *c);

/* Drops a reference; frees the conn when the last one is gone */
void conn_unref(conn_t *c);

/* Waits for any send in progress, then closes the socket. Safe to call more than once */
void conn_shutdown(conn_t *c);

#endif /* CONN_H */

