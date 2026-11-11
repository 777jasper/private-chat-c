#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "net_io.h"
#include "protocol.h"

#define PORT 5555

/* Connects to the server. Returns the socket, or -1 on failure. */
static int connect_server(void){

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd < 0){
		perror("socket");
		return -1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(PORT);

	if(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
		perror("connect");
		close(fd);
		return -1;
	}
	return fd;
}

/* Sends one frame, waits for the server's reply and prints it.
   Returns the reply's type (MSG_OK or MSG_ERROR), or -1 on failure. */
static int request(int fd, uint8_t type, const uint8_t *payload, uint32_t len){
	if(send_frame(fd, type, payload, len) == -1)
		return -1;
    	uint8_t  rtype;
    	uint8_t *reply;
    	uint32_t rlen;

	if(recv_frame(fd, &rtype, &reply, &rlen) == -1)
		return -1;
	printf("reply: type=%u text=\"%.*s\"\n", rtype, (int)rlen, reply ? (char *)reply : "");
	free(reply);
	return rtype;
}

/* Logs in. Returns 0 on success, -1 on failure. */
static int login(int fd, const char *user, const char *pass)
{
    size_t u = strlen(user);
    size_t p = strlen(pass);
    size_t total = u + 1 + p + 1;
    if (total > MAX_PAYLOAD)
        return -1;

    uint8_t *buf = malloc(total);
    if (buf == NULL)
        return -1;

    memcpy(buf, user, u + 1);          /* includes the 00 */
    memcpy(buf + u + 1, pass, p + 1);  /* includes the 00 */

    int t = request(fd, MSG_LOGIN, buf, (uint32_t)total);
    free(buf);
    return (t == MSG_OK) ? 0 : -1;
}

/* Sends one message. Returns 0 on success, -1 on failure. */
static int send_mode(int fd, const char *to, const char *text)
{
    size_t t = strlen(to);
    size_t b = strlen(text);
    size_t total = t + 1 + b;          /* recipient, one 00, body: no final 00 */
    if (total > MAX_PAYLOAD)
        return -1;

    uint8_t *buf = malloc(total);
    if (buf == NULL)
        return -1;
    
    memcpy(buf, to, t + 1);        /* includes the 00 separator */
    memcpy(buf + t + 1, text, b);  /* the body: NO 00 after it */

    int r = request(fd, MSG_SEND, buf, (uint32_t)total);
    free(buf);
    return (r == MSG_OK) ? 0 : -1;
}

/* Waits for deliveries and prints them until the connection closes. */
static int listen_mode(int fd)
{
    printf("listening...\n");
    for (;;) {
        uint8_t  type;
        uint8_t *payload;
        uint32_t len;

	if(recv_frame(fd, &type, &payload, &len) == -1){
		printf("connection closed\n");
		return 0;
	}


        if (type == MSG_DELIVER) {
		const uint8_t *sep = (len > 0) ? memchr(payload, 0, len) : NULL;
		if(sep == NULL){
			printf("malformed delivery\n");
		}
		else{
			size_t body_len = len - (size_t)(sep - payload) -1;
			printf("from %s: %.*s\n", (const char *)payload, (int)body_len, (const char*)(sep + 1));
		}
		
        }
        free(payload);
    }
}

int main(int argc, char *argv[])
{
    int mode;     /* 0 = listen, 1 = send */
    if(argc == 4 && strcmp(argv[1],"listen") == 0) mode =0;
    else if(argc == 6 && strcmp(argv[1], "send") == 0) mode = 1;
    else{
	    fprintf(stderr, "usage:\n %s listen user pass\n"
			    " %s send user pass recipient text\n", argv[0], argv[0]);
	    return 1;
    }

    int fd = connect_server();
    if (fd == -1)
        return 1;

    if (login(fd, argv[2], argv[3]) == -1) {
        close(fd);
        return 1;
    }

    int rc = (mode == 0) ? listen_mode(fd) : send_mode(fd, argv[4], argv[5]);
    close(fd);
    return (rc == 0) ? 0 : 1;
}
