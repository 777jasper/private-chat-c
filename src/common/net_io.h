#ifndef NET_IO_H
#define NET_IO_H

#include<stddef.h> /* size_t */
#include<stdint.h> /* uint8_t, uint32_t */

/* Send exactly len bytes from buf to the socket fd.
 * Returns 0 on success, -1 on failure. */

int send_all(int fd, const void *buf, size_t len);

/* Receive exactly len bytes from socket fd to the buf.
 * Returns 0 on success, -1 on failure or if the connection was closed before len bytes arrived. */

int recv_all(int fd, void *buf, size_t len);

int send_frame(int fd, uint8_t type, const uint8_t *payload, uint32_t len);

int recv_frame(int fd, uint8_t *type, uint8_t **payload, uint32_t *len);

#endif

