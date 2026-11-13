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
#include "msgcrypt.h"

#define PORT      5555
#define USER_BUF  64
#define PASS_BUF  256

#define KEYCACHE_MAX 16
#define KC_NAME_SIZE 33 /* 32 characters plus final 00 */

#define PENDING_MAX 32

typedef struct{
	char sender[KC_NAME_SIZE];
	uint8_t *body;	/* malloc'd copy of "nonce | ciphertext" */
	size_t body_len;
}pending_t;

typedef struct{
	int used;
	char name[KC_NAME_SIZE];
	unsigned char pk[crypto_box_PUBLICKEYBYTES];
}cached_key_t;

typedef struct{
	int fd;
	unsigned char pk[crypto_box_PUBLICKEYBYTES]; /* my public key */
	unsigned char sk[crypto_box_SECRETKEYBYTES]; /* my SECRET key: never sent, never printed */
	cached_key_t cache[KEYCACHE_MAX];
	size_t cache_next; /* slot to overwrite when a new name arrives */
	pending_t pending[PENDING_MAX];
	size_t pending_count;
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

/* Returns the cached key for name, or NULL if we don't have it. */
static const unsigned char *cache_find(const chat_ctx_t *ctx, const char *name){
	for(size_t i = 0; i<KEYCACHE_MAX; i++){
		if(ctx->cache[i].used && strcmp(ctx->cache[i].name, name) == 0)
			return ctx->cache[i].pk;
	}
	return NULL;
}


/* Stores a key. name must be at most KC_NAME_SIZE - 1 characters. */
static void cache_put(chat_ctx_t *ctx, const char *name, const unsigned char *pk){
	/* look for an existing entry with this name */
	for(size_t i = 0; i<KEYCACHE_MAX; i++){
		if(ctx->cache[i].used && strcmp(ctx->cache[i].name, name) == 0){
			memcpy(ctx->cache[i].pk, pk, crypto_box_PUBLICKEYBYTES);
			return;
		}
	}

	/* name not found, take the next slot */
	cached_key_t *e = &ctx->cache[ctx->cache_next];
	ctx->cache_next = (ctx->cache_next + 1) % KEYCACHE_MAX;
	e->used = 1;
	snprintf(e->name, sizeof e->name, "%s", name);
	memcpy(e->pk,pk, crypto_box_PUBLICKEYBYTES);
}

/* Prints a key as hex. note is "" or " (cached)". */
static void print_key(const char *name, const unsigned char *pk, const char *note){
	printf("key for %s:", name);
	for(size_t i = 0; i<crypto_box_PUBLICKEYBYTES; i++)
		printf("%02x", pk[i]);
	printf("%s\n", note);
}

/* Decrypts one message and prints it. */
static void show_message(const chat_ctx_t *ctx, const char *sender, const unsigned char *their_pk, const uint8_t *body, size_t body_len){

	uint8_t *plain;
	size_t plain_len;
	if(msgcrypt_decrypt(body, body_len, their_pk, ctx->sk, &plain, &plain_len) == -1){
		printf("from %s: [could not decrypt]\n", sender);
		return;
	}
	printf("from %s: %.*s\n", sender, (int)plain_len, (const char*)plain);
	free(plain);
}

/* Returns 1 if a message from sender is already waiting, otherwise 0. */
static int pending_has(const chat_ctx_t *ctx, const char *sender){

	for(size_t i = 0; i<ctx->pending_count; i++){
		if(strcmp(ctx->pending[i].sender, sender) == 0)
			return 1;
	}
	return 0;
}

/* Keeps a copy of an encrypted message until its sender's key is known.
   Returns 0 on success, -1 if the list is full or memory ran out. */
static int pending_add(chat_ctx_t *ctx, const char *sender, const uint8_t *body, size_t body_len){

	if(ctx->pending_count >= PENDING_MAX)
		return -1;
	uint8_t *copy = malloc(body_len > 0 ? body_len : 1);
	if(copy == NULL)
		return -1;
	memcpy(copy, body, body_len);
	pending_t *p = &ctx->pending[ctx->pending_count];
	snprintf(p->sender, sizeof p->sender, "%s", sender);
	p->body = copy;
	p->body_len = body_len;
	ctx->pending_count++;
	return 0;
}

/* Decrypts and prints every waiting message from sender, in order. */
static void pending_flush(chat_ctx_t *ctx, const char *sender, const unsigned char *their_pk){

	size_t keep = 0;
	for(size_t i = 0; i < ctx->pending_count; i++){
		pending_t *p = &ctx->pending[i];
		if(strcmp(p->sender, sender) == 0){
			show_message(ctx, p->sender, their_pk, p->body, p->body_len);
			free(p->body);
		}
		else{
			if(keep!=i)
				ctx->pending[keep] = *p;
			keep++;
		}
	}
	ctx->pending_count = keep;
}

/* Frees every waiting message. Call before the program exits. */
static void pending_free_all(chat_ctx_t *ctx){

	for(size_t i = 0; i < ctx->pending_count; i++)
		free(ctx->pending[i].body);
	ctx->pending_count = 0;
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
			const char *sender = (const char *)payload;
			size_t body_len = len - (size_t)(sep-payload)-1;

			const unsigned char *their_pk = cache_find(ctx,sender);
			if(their_pk == NULL){
				/* remember whether a request is already out BEFORE adding */
				int already_asked = pending_has(ctx, sender);
				if(pending_add(ctx, sender, sep + 1, body_len) == -1){
					printf("message from %s dropped: waiting list full\n", sender);
				}
				else if(!already_asked){
					send_frame(ctx->fd, MSG_GET_KEY, (const uint8_t *)sender, (uint32_t)strlen(sender));
				}
			}
			else{
				show_message(ctx, sender, their_pk, sep + 1, body_len);
			}
		}
	}
	else if(type == MSG_OK || type == MSG_ERROR){
		printf("[server] %.*s\n", (int)len, payload ? (char *)payload : "");
	}

	else if(type == MSG_KEY){
		/* find the separator */
		const uint8_t *sep = (len > 0) ? memchr(payload, 0, len) : NULL;
		if(sep == NULL || (size_t)(sep-payload)>KC_NAME_SIZE-1|| len - (size_t)(sep-payload)-1 != crypto_box_PUBLICKEYBYTES){
			printf("malformed key reply\n");
		}
		else{
			const char *who = (const char*)payload;
			int had_pending = pending_has(ctx, who);
			cache_put(ctx, who, sep + 1);
			if(had_pending)
				pending_flush(ctx, who, sep + 1);
			else
				print_key(who, sep + 1, "");

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
		const unsigned char *known = cache_find(ctx, name);
		if(known != NULL){
			print_key(name, known, " (cached)");
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

	/* cut the line after the recipient, so "line" is now a terminated name.
	 * body already points past the space, so it is not affected.*/
	*sp = '\0';

	/* look up the recipient's public key */
	const unsigned char *their_pk = cache_find(ctx, line);
	if(their_pk == NULL){
		printf("no key for %s yet; type /key %s first\n", line, line);
		return 0;
	}

	/* encrypt the body */
	uint8_t *enc;
	size_t enc_len;
	if(msgcrypt_encrypt((const uint8_t *)body, body_len, their_pk, ctx->sk, &enc, &enc_len) == -1){
		printf("encryption failed\n");
		return 0;
	}

	/* build "recipient 00 ciphertext" */
	size_t total = r_len + 1 + enc_len;
	uint8_t *buf = malloc(total);
	if(buf == NULL){
		free(enc);
		return -1;
	}
	memcpy(buf, line, r_len);
	buf[r_len] = 0;
	memcpy(buf + r_len + 1, enc, enc_len);
	free(enc);
	
	/* build "recipient 00 body" (no final 00 after the body) 
	size_t total = r_len + 1 + body_len;
	uint8_t *buf = malloc(total);
	if(buf == NULL)
		return -1;
	memcpy(buf, line, r_len);
	buf[r_len] = 0;
	memcpy(buf + r_len + 1, body, body_len);
*/
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
	    pending_free_all(&ctx);
	    sodium_memzero(ctx.sk, sizeof ctx.sk);
	    close(fd);
	    return (chat_rc == 0) ? 0 : 1;
    }

    close(fd);
    return (type == MSG_OK) ? 0 : 1;
}
