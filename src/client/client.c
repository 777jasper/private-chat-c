#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "net_io.h"
#include "protocol.h"
#include <poll.h>
#include <errno.h>
#include "keys.h"

#define PORT      5555
#define USER_BUF  64
#define PASS_BUF  256

typedef struct{
	int fd;
	unsigned char pk[crypto_box_PUBLICKEYBYTES]; /* my public key */
	unsigned char sk[crypto_box_SECRETKEYBYTES]; /* my SECRET key: never sent, never printed */
}chat_ctx_t;

/* Reads one line from the keyboard into buf (without the newline).
   Returns 0 on success, -1 on end of input or error. */
static int read_line(const char *prompt, char *buf, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);                      /* make the prompt appear now */

    if(fgets(buf, size, stdin) == NULL)
	    return -1;
    /* look for the newline */
    char *nl = strchr(buf, '\n');
    if(nl != NULL)
	    *nl = '\0'; /* cut the line at the newline */
    else{
	    /* the line was longer than buf; throw away the rest */
	    int ch;
	    while((ch = getchar()) != '\n' && ch!=EOF);
    }
    return 0;
}

/* Same as read_line, but the typed characters are not shown. */
static int read_password(const char *prompt, char *buf, size_t size)
{
    struct termios old, t;

    if(tcgetattr(0, &old) == -1){
	    perror("tcgetattr");
	    return -1;
    }
    t = old;
    t.c_lflag &= ~ECHO;
    if(tcsetattr(0, TCSANOW, &t) == -1){
	    perror("tcsetattr");
	    return -1;
    }

    int rc = read_line(prompt, buf, size);
    
    /* put the old settings back */
    tcsetattr(0, TCSANOW, &old);
    printf("\n");
    return rc;
}

/* Connects to the server. Returns the socket, or -1 on failure. */
static int connect_server(void)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd < 0){
		perror("socket");
		return -1;
	}

	struct sockaddr_in addr;
	memset(&addr,0, sizeof(addr));
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

/* Handles one frame from the server. Returns 0 to keep going, -1 if the connection is gone. */
static int handle_incoming(chat_ctx_t *ctx)
{
	uint8_t type;
	uint8_t *payload;
	uint32_t len;

	if(recv_frame(ctx->fd, &type, &payload, &len) == -1){
		printf("server closed the connection\n");
		return -1;
	}

	if(type == MSG_DELIVER){
		const uint8_t *sep = (len > 0) ? memchr(payload, 0, len) : NULL;
		if(sep == NULL)
			printf("malformed delivery\n");
		else{
			size_t body_len = len - (size_t)(sep - payload) - 1;
			printf("from %s: %.*s\n", (const char *)payload, (int)body_len, (const char*)(sep + 1));
		}
	}
	else if(type == MSG_OK || type == MSG_ERROR){
		printf("[server] %.*s\n", (int)len, payload ? (char *)payload : "");
	}

	else if(type == MSG_KEY){
		/* find the separator */
		const uint8_t *sep = (len > 0) ? memchr(payload, 0, len) : NULL;
		if(sep == NULL || len - (size_t)(sep-payload)-1 != crypto_box_PUBLICKEYBYTES){
			printf("malformed key reply\n");
		}
		else{
			printf("key for %s:", (const char*)payload);
			for(size_t i = 0; i<crypto_box_PUBLICKEYBYTES; i++){
				printf("%02x", sep[i+1]);
			}
			printf("\n");
		}
	}

	free(payload);
	return 0;
}

/* Handles one typed line "recipient message". Returns 0 to keep going, 1 to quit, -1 if sending failed. */
static int handle_typed(chat_ctx_t *ctx, char *line){
	if(strcmp(line, "/quit") == 0)
		return 1;

	/* "/key name" command */
	if(strncmp(line, "/key ", 5) == 0){
		const char *name = line + 5;
		size_t n = strlen(name);
		if(n == 0){
			printf("usage: /key username\n");
			return 0;
		}
		int rc = send_frame(ctx->fd, MSG_GET_KEY, (const uint8_t *)name, (uint32_t)n);
		return (rc == 0) ? 0 : -1;
	}

	char *sp = strchr(line, ' ');
	if(sp == NULL || sp == line || sp[1] == '\0'){
		printf("usage: recipient message (or /quit)\n");
		return 0;
	}
	size_t r_len = (size_t)(sp - line);
	const char *body = sp + 1;
	size_t body_len = strlen(body);

	/* build "recipient 00 body" (no final 00 after the body) */
	size_t total = r_len + 1 + body_len;
	uint8_t *buf = malloc(total);
	if(buf == NULL)
		return -1;
	memcpy(buf, line, r_len);
	buf[r_len] = 0;
	memcpy(buf + r_len + 1, body, body_len);

	int rc = send_frame(ctx->fd, MSG_SEND, buf, (uint32_t)total);
	free(buf);
	return (rc == 0) ? 0 : -1;
}

/* Runs until the user quits or the connection is lost */
static int chat_loop(chat_ctx_t *ctx){
	printf("Logged in. Type: recipient message (or /quit)\n");

	struct pollfd fds[2];
	fds[0].fd = 0;
	fds[0].events = POLLIN;
	fds[1].fd = ctx->fd;
	fds[1].events = POLLIN;

	for(;;){
		int n = poll(fds, 2, -1);
		if(n<0){
			if(errno == EINTR)
				continue;
			perror("poll");
			return -1;
		}

		if(fds[1].revents & (POLLIN | POLLHUP | POLLERR)){
			if(handle_incoming(ctx) == -1)
				return 0;
		}

		if(fds[0].revents & (POLLIN | POLLHUP)){
			char line[1024];

			if(read_line("", line,sizeof line) == -1)
				return 0; /* CTRL+D: leave */
			int r = handle_typed(ctx, line);
			if(r==1)
				return 0; /* /quit */
			if(r==-1){
				printf("send failed\n");
				return -1;
			}
		}
	}
}

int main(void)
{
    char command[USER_BUF];
    char username[USER_BUF];
    char password[PASS_BUF];

    /* read the three values */
    if(read_line("Command (register/login): ", command, sizeof command) == -1 ||
	read_line("Username: ", username, sizeof username) == -1 ||
	read_password("Password: ", password, sizeof password) == -1)
	    return 1;

    /* choose the message type from the command */
    uint8_t msg_type;
    if(strcmp(command, "register") == 0)
	    msg_type = MSG_REGISTER;
    else if(strcmp(command, "login") == 0)
	    msg_type = MSG_LOGIN;
    else{
	    explicit_bzero(password, sizeof password); /* wipe the password */
	    fprintf(stderr, "unknown command: %s\n", command);
	    return 1;
    }

    /* build the payload "username 00 password 00" in malloc'd buffer(size u_len + 1 + p_len + 1) */
    size_t u_len = strlen(username);
    size_t p_len = strlen(password);
    size_t total = u_len + 1 + p_len +1;

    if(total > MAX_PAYLOAD){
	    fprintf(stderr, "input too long\n");
	    explicit_bzero(password, sizeof password);
	    return 1;
    }

    uint8_t *payload = malloc(total);
    if(payload == NULL){
	    explicit_bzero(password, sizeof password);
	    perror("malloc");
	    return 1;
    }

    /* copy the username including its 00 into the start of payload */
    memcpy(payload, username, u_len + 1);

    /* copy the password including its 00 right after it */
    memcpy(payload + u_len + 1, password, p_len + 1);

    /* wipe the password as soon as it has been copied */
    explicit_bzero(password, sizeof password);

    /* connect */
    int fd = connect_server();
    if(fd == -1){
	    explicit_bzero(payload, total); /* it still holds the password */
	    free(payload);
	    return 1;
    }

    /* send, then wipe and free the payload on EVERY outcome */
    int rc = send_frame(fd, msg_type, payload, (uint32_t)total);
    explicit_bzero(payload, total);
    free(payload);
    if(rc == -1){
	    fprintf(stderr, "send failed\n");
	    close(fd);
	    return 1;
    }

    /* read the reply */
    uint8_t type;
    uint8_t *reply;
    uint32_t reply_len;
    if(recv_frame(fd, &type, &reply, &reply_len) == -1){
	    fprintf(stderr, "no reply from server\n");
	    close(fd);
	    return 1;
    }

    /* print it, clean up, and report success or failure */
    printf("%.*s\n", (int)reply_len, reply ? (char *)reply: "");
    free(reply);
    if(type == MSG_OK && msg_type == MSG_LOGIN){
	    /* start the libsodium and load (or create) this user's keys */
	    if(sodium_init() < 0){
		    fprintf(stderr,"libsodium failed to initialize\n");
		    close(fd);
		    return 1;
	    }

	    chat_ctx_t ctx = {0};
	    ctx.fd = fd;
	    /* load or create the keys directly into the struct */
	    if(keys_load_or_create(username, ctx.pk, ctx.sk) == -1){
		    sodium_memzero(ctx.sk, sizeof ctx.sk);
		    close(fd);
		    return 1;
	    }
	    
	    /* upload the public key. DO NOT wipe sk here any more */
	    int krc = send_frame(fd, MSG_SET_KEY, ctx.pk, sizeof ctx.pk);
	    if(krc == -1){
		    fprintf(stderr, "could not send key\n");
		    sodium_memzero(ctx.sk, sizeof ctx.sk);
		    close(fd);
		    return 1;
	    }

	    /* run the loop, then wipe the secret key on the way out */
	    int chat_rc = chat_loop(&ctx);
	    sodium_memzero(ctx.sk, sizeof ctx.sk);
	    close(fd);
	    return (chat_rc == 0) ? 0 : 1;
    }

    close(fd);
    return (type == MSG_OK) ? 0 : 1;
}
