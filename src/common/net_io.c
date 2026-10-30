#include<stdlib.h>
#include<string.h>
#include<arpa/inet.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/socket.h>
#include"net_io.h"
#include"protocol.h"

int send_all(int fd, const void *buf, size_t len){

	const uint8_t *p = buf; /* p walks through the data as bytes */
	size_t sent = 0; /* how many bytes have gone out so far */

	while(sent<len){
		ssize_t n = send(fd, p+sent, len-sent, MSG_NOSIGNAL);

		if(n<0){
			if(errno == EINTR)
				continue;
			return -1;
		}

		else if(n==0){
			return -1; /* nothing was sent */
		}

		else
			sent = sent + n;
	}
	return 0;
}

int recv_all(int fd, void *buf, size_t len){

	uint8_t *p = buf;
	size_t got = 0;

	while(got<len){
		ssize_t n = recv(fd, p+got, len-got, 0);

		if(n<0){
			if(errno == EINTR)
				continue;
			return -1;
		}
		else if(n==0)
			return -1; /* connection closed */
		else
			got = got + n;
	}
	return 0;
}

int send_frame(int fd, uint8_t type, const uint8_t *payload, uint32_t len){

	if(len>MAX_PAYLOAD)
		return -1;
	uint8_t header[HEADER_SIZE];
	uint32_t net_len = htonl(len);
	memcpy(header, &net_len, 4);
	header[4] = type;
	if(send_all(fd, header, HEADER_SIZE)<0)
		return -1;
	if(send_all(fd, payload, len)<0)
		return -1;
	return 0;
}

int recv_frame(int fd, uint8_t *type, uint8_t **payload, uint32_t *len){

	uint8_t header[HEADER_SIZE];
	
	/* Read exactly 5 header bytes. */
	if(recv_all(fd, header, HEADER_SIZE)<0)
		return -1;

	/* pull the length out of the first 4 bytes and fix the byte order */
	uint32_t net_len;
	memcpy(&net_len, header, 4);
	uint32_t n = ntohl(net_len);

	/* Reject oversized frames BEFORE allocating any memory */
	if(n>MAX_PAYLOAD)
		return -1;

	uint8_t *buf = NULL; /* stays NULL when there is no payload */

	if(n>0){
		buf = malloc(n);
		if(buf == NULL)
			return -1;
		if(recv_all(fd, buf, n) < 0){
			free(buf); /* don't leak memory if the read fails */
			return -1;
		}
	}

	/* Everything succeeded, so now fill in the caller's variables. */

	*type = header[4];
	*payload = buf;
	*len = n;
	return 0;
}

