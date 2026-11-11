#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "registry.h"

typedef struct{
	int used;	/* 0 = free slot, 1 = in use */
	conn_t *conn;
	char username[REGISTRY_NAME_SIZE];
}entry_t;

/* Private to this file. Every access must hold table_lock */
static entry_t table[REGISTRY_MAX]; /* starts all zero: every slot free */
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

int registry_add(const char *username, conn_t *c){
	int result = -1; /* assume "full" until we find a slot. */
	int free_slot = -1;

	pthread_mutex_lock(&table_lock);

	for(int i = 0; i<REGISTRY_MAX; i++){
		if(!table[i].used){
			if(free_slot < 0)
				free_slot = i; /* remember the free slot */
		}
		else if(strcmp(table[i].username, username)==0){
			result = 1; /* already logged in */
			free_slot = -2; /* marker: do not add */
			break;
		}
	}
		if(free_slot >= 0){
			table[free_slot].used = 1;
			table[free_slot].conn =c;
			conn_ref(c);
			snprintf(table[free_slot].username, REGISTRY_NAME_SIZE, "%s", username);
			result = 0;
		}

		pthread_mutex_unlock(&table_lock);
		return result;
}

void registry_remove(conn_t *c){

	conn_t *found = NULL;
	pthread_mutex_lock(&table_lock);

	for(int i = 0; i<REGISTRY_MAX; i++){
		if(table[i].used && table[i].conn == c){
			table[i].used = 0;
			table[i].conn = NULL;
			found = c;
			break;
		}
	}
		pthread_mutex_unlock(&table_lock);
		if(found)
			conn_unref(found);
}

conn_t *registry_lookup(const char *username){

	conn_t *result = NULL;
	pthread_mutex_lock(&table_lock);

	for(int i = 0; i<REGISTRY_MAX; i++){
		if(table[i].used && strcmp(table[i].username, username) == 0){
			result = table[i].conn;
			conn_ref(result); /* reference taken WHILE locked */
			break;
		}
	}
		pthread_mutex_unlock(&table_lock);
		return result;
}
