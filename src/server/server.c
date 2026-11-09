#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include "net_io.h"
#include "protocol.h"
#include "db.h"
#include "auth.h"

#define PORT 5555
#define BACKLOG 16

#define USERNAME_MIN 3
#define USERNAME_MAX 32
#define PASSWORD_MIN 8
#define PASSWORD_MAX 128

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
static int send_text(int fd, uint8_t type, const char *text){
	
	int result = send_frame(fd, type, (const uint8_t *)text, strlen(text));
	return result;
}

/* Handles one MSG_REGISTER frame. Returns 0 if a reply was sent
   (even an error reply), -1 if the connection is broken. */
static int handle_register(int fd, const uint8_t *payload, uint32_t len){
	const char *username;
    	const char *password;

    	if(parse_credentials(payload, len, &username, &password) == -1)
	    	return send_text(fd, MSG_ERROR, "invalid username or password");

    	char hash[AUTH_HASH_SIZE];
	
	if(auth_hash_password(password, hash, sizeof hash) == -1)
		return send_text(fd, MSG_ERROR, "server error");
	
	int rc = db_add_user(username, hash);
	
	if(rc == 0)
		return send_text(fd, MSG_OK, "registered");
	
	if(rc == 1)
		return send_text(fd, MSG_ERROR, "username unavailable");
	
	return send_text(fd, MSG_ERROR, "server error");
}

/* serves one client until it disconnects */
static void handle_client(int client_fd){

	for(;;){
		uint8_t type;
		uint8_t *payload;
		uint32_t len;

		if(recv_frame(client_fd, &type, &payload, &len)<0){
			
			break;
		}

		int rc;
		switch(type){
			case MSG_REGISTER: rc = handle_register(client_fd, payload, len);
					   break;
			default:
					   rc = send_text(client_fd, MSG_ERROR, "unsupported request");
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

	handle_client(client_fd);
	close(client_fd);

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

