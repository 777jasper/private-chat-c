#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>
#include "net_io.h"
#include "protocol.h"
#include "db.h"
#include "auth.h"
#include "conn.h"
#include "registry.h"

#define PORT 5555
#define BACKLOG 16

#define USERNAME_MIN 3
#define USERNAME_MAX 32
#define PASSWORD_MIN 8
#define PASSWORD_MAX 128

#define SEND_TIMEOUT_SEC 10

#define HISTORY_COUNT 50

typedef struct{
	int logged_in; /* 0 = no, 1 = yes */
	char username[USERNAME_MAX+1]; /* valid only when logged_in is 1 */
}session_t;


/* Splits a register payload ("username 00 password 00") into two strings.
   On success returns 0 and points *username and *password INSIDE payload
   (they are only valid until payload is freed). Returns -1 if malformed. */
static int parse_credentials(const uint8_t *payload, uint32_t len,const char **username, const char **password){

	/* payload must end with a 00 byte.*/
	if(len == 0 || payload[len-1] != 0)
		return -1;
	/* the first 00 byte (the end of the username). */
	const uint8_t *sep = memchr(payload, 0, len);

	if(sep == NULL)
		return -1;
	/* the first 00 is also the last byte, so there is no password part. */
	if(sep == payload + len -1)
		return -1;

	size_t u_len = sep - payload;
	size_t p_len = len - u_len -2;

	/* check length of username, password. */
	if(u_len < USERNAME_MIN || u_len > USERNAME_MAX || p_len < PASSWORD_MIN || p_len > PASSWORD_MAX)
		return -1;

	/* the password must not contains another 00 byte. */
	if(memchr(sep+1,0,p_len) != NULL)
		return -1;

	/* check every username character. */
	for(size_t i = 0; i<u_len; i++){
		unsigned char c = payload[i];
		if(!isalnum(c) && c!='_')
			return -1;
	}

	/* return username, password */
	*username = (const char *)payload;
	*password = (const char *)(sep + 1);
	return 0;
}

/* Sends a short text reply (MSG_OK or MSG_ERROR) to the client.
   Returns 0 on success, -1 if sending failed. */
static int send_text(conn_t *c, uint8_t type, const char *text){
	
	return conn_send(c, type, (const uint8_t *)text, strlen(text));
}

/* Handles one MSG_REGISTER frame. Returns 0 if a reply was sent
   (even an error reply), -1 if the connection is broken. */
static int handle_register(conn_t *c, const uint8_t *payload, uint32_t len){
	const char *username;
    	const char *password;

    	if(parse_credentials(payload, len, &username, &password) == -1)
	    	return send_text(c, MSG_ERROR, "invalid username or password");

    	char hash[AUTH_HASH_SIZE];
	
	if(auth_hash_password(password, hash, sizeof hash) == -1)
		return send_text(c, MSG_ERROR, "server error");
	
	int rc = db_add_user(username, hash);
	
	if(rc == 0)
		return send_text(c, MSG_OK, "registered");
	
	if(rc == 1)
		return send_text(c, MSG_ERROR, "username unavailable");
	
	return send_text(c, MSG_ERROR, "server error");
}

/* Sends every undelivered message waiting for username, oldest first. Stops at the first failure; whatever is left waits for the next login. */
static void deliver_backlog(conn_t *c, const char *username){

	for(;;){
		int64_t id;
		char sender[USERNAME_MAX + 1];
		uint8_t *body;
		size_t body_len;

		int rc = db_next_undelivered(username, &id, sender, sizeof sender, &body, &body_len);
		if(rc!=0)
			break; /* 1 = nothing waiting, -1 = error */

		/* build the frame payload "sender 00 body" */
		size_t s_len = strlen(sender);
		size_t total = s_len + 1 + body_len;
		uint8_t *out = (total <= MAX_PAYLOAD) ? malloc(total) : NULL;
		if(out == NULL){
			free(body);
			break;
		}
		memcpy(out, sender, s_len + 1); // includes the 00
		memcpy(out+s_len+1, body, body_len);
		free(body);

		int sent = conn_send(c, MSG_DELIVER, out, (uint32_t)total);
		free(out);
		if(sent == -1)
			break;
		if(db_mark_delivered(id) == -1)
			break;
	}
}

/* Handles on MSG_LOGIN frame. Returns 0 if a reply was send, -1 if the connection is broken. */
static int handle_login(conn_t *c, const uint8_t *payload, uint32_t len, session_t *sess){

	if(sess->logged_in != 0)
		return send_text(c, MSG_ERROR, "already logged in");

	const char *username;
	const char *password;

	if(parse_credentials(payload, len, &username, &password) == -1)
		return send_text(c, MSG_ERROR, "invalid username or password");

	char hash[AUTH_HASH_SIZE];
	int ok = 0;

	int rc = db_get_hash(username, hash, sizeof hash);

	if(rc == 0){
		ok = (auth_verify_password(hash, password) == 0);
	}
	else if(rc == 1){
		auth_dummy_verify(password);
	}
	else{
		return send_text(c, MSG_ERROR, "server error");
	}

	if(!ok)
		return send_text(c, MSG_ERROR, "invalid username or password");
	
	int added = registry_add(username, c);
	if(added == 1)
		return send_text(c, MSG_ERROR, "already logged in");
	if(added == -1)
		return send_text(c, MSG_ERROR, "server error");

	snprintf(sess->username, sizeof sess->username, "%s", username);
	sess->logged_in = 1;

	rc = send_text(c, MSG_OK, "logged in");
	if(rc == -1)
		return -1;
	deliver_backlog(c, sess->username);
	return 0;
}

/* Handles one MSG_SEND frame ("recipient 00 body"). Returns 0 if a reply
   was sent to the sender, -1 if the sender's connection is broken. */
static int handle_send(conn_t *c, const uint8_t *payload, uint32_t len, const session_t *sess){
	if(!sess->logged_in)
		return send_text(c, MSG_ERROR, "not logged in");

	if(len == 0)
		return send_text(c, MSG_ERROR, "invalid message");
	
	const uint8_t *sep = memchr(payload, 0, len);
	if(sep == NULL || sep == payload+len-1)
		return send_text(c, MSG_ERROR, "invalid message");

	size_t r_len = sep - payload;
	size_t body_len = len -r_len -1;
	if(r_len < USERNAME_MIN || r_len > USERNAME_MAX || body_len > MAX_PAYLOAD - USERNAME_MAX -1)
		return send_text(c, MSG_ERROR, "invalid message");

	/* check each recipient character, like parse_credentials */
	for(size_t i = 0; i < r_len; i++){
		unsigned char ch = payload[i];
		if(!isalnum(ch) && ch !='_')
			return send_text(c, MSG_ERROR, "invalid message");
	}

	/* copy the recipient into a terminated string */
	char recipient[USERNAME_MAX + 1];
	memcpy(recipient, payload, r_len);
	recipient[r_len] = '\0';

	int64_t msg_id = 0;
	/* store the message before trying to deliver it */
	int st = db_store_message(sess->username, recipient, sep + 1, body_len, &msg_id);
	if(st == 1)
		return send_text(c, MSG_ERROR, "unknown user");
	if(st == -1)
		return send_text(c, MSG_ERROR, "server error");

	conn_t *dest = registry_lookup(recipient);
	if(dest == NULL)
		return send_text(c, MSG_OK, "sent");

	/* build the delivery payload "sender 00 body" */
	size_t s_len = strlen(sess->username);
	size_t total = s_len + 1 + body_len;
	uint8_t *out = malloc(total);
	if(out == NULL){
		conn_unref(dest);
		return send_text(c, MSG_ERROR, "server error");
	}
	memcpy(out, sess->username, s_len+1); /* includes the 00 */
	memcpy(out + s_len + 1, sep + 1, body_len);

	int rc = conn_send(dest, MSG_DELIVER, out, (uint32_t)total);
	free(out);
	conn_unref(dest);

	if(rc == 0)
		db_mark_delivered(msg_id);

	return send_text(c, MSG_OK, "sent");
}

/* Handles one MSG_SET_KEY frame. Returns 0 if a reply was sent, -1 if the connection is broken. */
static int handle_set_key(conn_t *c, const uint8_t *payload, uint32_t len, const session_t *sess){

	if(!sess->logged_in)
		return send_text(c, MSG_ERROR, "not logged in");
	if(len != DB_PUBKEY_SIZE)
		return send_text(c, MSG_ERROR, "invalid key");
	int rc = db_set_pubkey(sess->username, payload);
	if(rc == 0)
		return send_text(c, MSG_OK, "key saved");
	return send_text(c, MSG_ERROR, "server error");
}

/* Handles one MSG_GET_KEY frame (payload = a username, no 00). Returns 0 if a reply was sent, -1 if the connection is broken. */
static int handle_get_key(conn_t *c, const uint8_t *payload, uint32_t len, const session_t *sess){
	if(!sess->logged_in)
		return send_text(c, MSG_ERROR, "not logged in");

	if(len < USERNAME_MIN || len > USERNAME_MAX)
		return send_text(c, MSG_ERROR, "invalid username");

	for(size_t i = 0; i<len; i++){
		unsigned char ch = payload[i];
		if(!isalnum(ch) && ch!='_')
			return send_text(c, MSG_ERROR, "invalid username");
	}
	/* copy the name into a terminated string */
	char name[USERNAME_MAX + 1];
	memcpy(name, payload, len);
	name[len] = '\0';

	unsigned char key[DB_PUBKEY_SIZE];
	int rc = db_get_pubkey(name, key);
	if(rc == 1)
		return send_text(c, MSG_ERROR, "no key available");
	if(rc == -1)
		return send_text(c, MSG_ERROR, "server error");

	/* build the "username 00 key" in a stack buffer */
	uint8_t out[USERNAME_MAX + 1 + DB_PUBKEY_SIZE];
	memcpy(out, name, len + 1); /* includes the 00 */
	memcpy(out + len + 1, key, DB_PUBKEY_SIZE);
	return conn_send(c, MSG_KEY, out, (uint32_t)(len + 1 + DB_PUBKEY_SIZE));
}

/* Handles one MSG_HISTORY_REQ frame. Returns 0 on success (or after an  error reply), -1 if the connection is broken. */
static int handle_history(conn_t *c, const uint8_t *payload, uint32_t len, const session_t *sess){
	if(!sess->logged_in)
		return send_text(c, MSG_ERROR, "not logged in");
	if(len < USERNAME_MIN || len > USERNAME_MAX)
		return send_text(c, MSG_ERROR, "invalid username");
	for(size_t i = 0; i < len; i++){
		unsigned char ch = payload[i];
		if(!isalnum(ch) && ch!='_')
			return send_text(c, MSG_ERROR, "invalid username");
	}
	char peer[USERNAME_MAX + 1];
	memcpy(peer, payload, len);
	peer[len] = '\0';

	int64_t after;
	if(db_history_start(sess->username, peer, HISTORY_COUNT, &after) == -1)
		return send_text(c, MSG_ERROR, "server error");
	int db_failed = 0;

	for(int n = 0; n < HISTORY_COUNT; n++){
		int64_t id, sent_at;
		int from_me;
		uint8_t *body;
		size_t body_len;

		int rc = db_history_next(sess->username, peer, after, &id, &from_me, &sent_at, &body, &body_len);
		if(rc == -1)
			db_failed = 1;
		if(rc != 0)
			break;
		after = id; /* always advance, even if we skip this one */
		size_t p_len = len; /* the peer name length */
		size_t total = p_len + 1 + 1 + 8 + body_len;
		if(total > MAX_PAYLOAD){
			free(body);
			continue;
		}
		uint8_t *out = malloc(total);
		if(out == NULL) {
			free(body);
			db_failed = 1;
			break;
		}

		/* fill the frame */
		memcpy(out, peer, p_len + 1);
		out[p_len + 1] = (uint8_t)(from_me ? 1 : 0);
		for(int i = 0; i < 8; i++)
			out[p_len + 2 + i] = (uint8_t)((uint64_t)sent_at >> (56 - 8 * i));
		memcpy(out + p_len + 10, body, body_len);
		free(body);

		int sent = conn_send(c, MSG_HISTORY_ITEM, out, (uint32_t)total);
		free(out);
		if(sent == -1)
			return -1;
	}
		if(db_failed)
			return send_text(c,MSG_ERROR, "server error");
		return conn_send(c, MSG_HISTORY_END, (const uint8_t *)peer, (uint32_t)len);
}

/* serves one client until it disconnects */
static void handle_client(conn_t *c){

	session_t sess = {0};

	for(;;){
		uint8_t type;
		uint8_t *payload;
		uint32_t len;

		if(recv_frame(c->fd, &type, &payload, &len)<0){
			
			break;
		}

		int rc;
		switch(type){
			case MSG_REGISTER: rc = handle_register(c, payload, len);
					   break;
			
			case MSG_LOGIN: rc = handle_login(c, payload, len, &sess);
					break;
			case MSG_SEND:	rc = handle_send(c, payload, len, &sess);
					break;
			case MSG_SET_KEY: rc = handle_set_key(c, payload, len, &sess);
					  break;
			case MSG_GET_KEY: rc = handle_get_key(c, payload, len, &sess);
					  break;
			case MSG_HISTORY_REQ: rc = handle_history(c, payload, len, &sess);
					      break;
			default:	rc = send_text(c, MSG_ERROR, "unsupported request");
					break;
		}


		free(payload);
		if(rc==-1){
			
			break;
		}
	}
}

/* Runs in its own thread, one per client */
static void *client_thread(void *arg){

	/* copy the descriptor out of the block main gave us, then free it */
	int client_fd = *(int *)arg;
	free(arg);

	struct timeval tv;
	tv.tv_sec = SEND_TIMEOUT_SEC;
	tv.tv_usec = 0;
	if(setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) == -1)
		perror("setsockopt");

	conn_t *c = conn_create(client_fd);
	if(c==NULL){
		close(client_fd);
		return NULL;
	}
	handle_client(c);
	registry_remove(c);
	conn_shutdown(c); /* closes the socket exactly once. */
	conn_unref(c); /* drops our reference; frees c at zero. */
	return NULL;
}

int main(void){

	/* open the database. */
	if(db_init("data/chat.db") == -1){
		fprintf(stderr, "server: database setup failed\n");
		return 1;
	}

	if(auth_init() == -1){
		fprintf(stderr, "server: crypto setup failed\n");
		db_close();
		return 1;
	}



	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_fd < 0){
		perror("socket");
		return 1;
	}

	int yes = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	server_addr.sin_port = htons(PORT);

	if(bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr))<0){
		perror("bind");
		close(listen_fd);
		return 1;
	}

	if(listen(listen_fd, BACKLOG)<0){
		perror("listen");
		close(listen_fd);
		return 1;
	}

	printf("Server listening on 127.0.0.1:%d\n", PORT);

	for(;;){
		int client_fd = accept(listen_fd, NULL, NULL);
		if(client_fd<0){
			if(errno == EINTR)
				continue;
			else{
				perror("accept");
				continue;
			}
		}

		int *arg = malloc(sizeof *arg);
		if(arg == NULL){
			close(client_fd);
			continue;
		}
		*arg = client_fd;
		
		/* start the thread */
		pthread_t tid;

		int rc = pthread_create(&tid, NULL, client_thread, arg);

		if(rc!=0){
			fprintf(stderr, "pthread_create: %s\n", strerror(rc));
			free(arg);
			close(client_fd);
			continue;
		}
		pthread_detach(tid);
	}

	close(listen_fd); // close listen_fd
	db_close(); // close the database
	return 0;
}

