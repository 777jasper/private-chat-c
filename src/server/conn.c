#include <stdlib.h>
#include <unistd.h>
#include "conn.h"
#include "net_io.h"

conn_t *conn_create(int fd){

	conn_t *c = malloc(sizeof *c);
	if(c == NULL)
		return NULL;

	c->fd = fd;
	c->alive = 1;
	atomic_init(&c->refs, 1);

	if(pthread_mutex_init(&c->send_lock, NULL) != 0){
		free(c);
		return NULL;
	}
	return c;
}

int conn_send(conn_t *c, uint8_t type, const uint8_t *payload, uint32_t len){

	int rc = -1;

	pthread_mutex_lock(&c->send_lock);
	if(c->alive)
		rc = send_frame(c->fd, type, payload, len);
	pthread_mutex_unlock(&c->send_lock);

	return rc;
}

void conn_ref(conn_t *c){

	atomic_fetch_add(&c->refs, 1);
}

void conn_unref(conn_t *c){

	if(atomic_fetch_sub(&c->refs, 1) == 1){
		pthread_mutex_destroy(&c->send_lock);
		free(c);
	}
}

void conn_shutdown(conn_t *c){
	pthread_mutex_lock(&c->send_lock);

	if(c->alive){
		c->alive = 0;
		close(c->fd);
	}
	pthread_mutex_unlock(&c->send_lock);
}

