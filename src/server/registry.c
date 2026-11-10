#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "registry.h"

typedef struct{
	int used;	/* 0 = free slot, 1 = in use */
	int fd;
	char username[REGISTRY_NAME_SIZE];
}entry_t;

/* Private to this file. Every access must hold table_lock */
static entry_t table[REGISTRY_MAX]; /* starts all zero: every slot free */
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

int registry_add(const char *username, int fd){
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
			table[free_slot].fd =fd;
			snprintf(table[free_slot].username, REGISTRY_NAME_SIZE, "%s", username);
			result = 0;
		}

		pthread_mutex_unlock(&table_lock);
		return result;
}

void registry_remove(int fd){

	pthread_mutex_lock(&table_lock);
	for(int i = 0; i<REGISTRY_MAX; i++){
		if(table[i].used && table[i].fd == fd){
			table[i].used = 0;
			break;
		}
	}
		pthread_mutex_unlock(&table_lock);
}
