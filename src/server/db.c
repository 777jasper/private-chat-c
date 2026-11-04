#include <stdio.h>
#include "sqlite3.h"
#include "db.h"

/* The one shared connection. "static" makes it private to this file. */
static sqlite3 *db = NULL;

/* Adjacent string literals in C are joined into one string. */
static const char *SCHEMA_SQL =
	"CREATE TABLE IF NOT EXISTS users ("
	"id	INTEGER PRIMARY KEY AUTOINCREMENT,"
	"username TEXT NOT NULL UNIQUE,"
	"pw_hash TEXT NOT NULL"
	");";

int db_init(const char *path){

	/* open the dataase. */
	int rc = sqlite3_open(path, &db);
	if(rc!=SQLITE_OK){
		fprintf(stderr,"db: cannot open: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		db = NULL;
		return -1;
	}

	/* wait up to 5 seconds when the database is busy. */
	sqlite3_busy_timeout(db,5000);

	/* create the table. */
	char *errmsg = NULL;
	rc = sqlite3_exec(db, SCHEMA_SQL, NULL, NULL, &errmsg);
	if(rc!=SQLITE_OK){
		fprintf(stderr,"db: cannot create tables: %s\n", errmsg);
		sqlite3_free(errmsg);
		sqlite3_close(db);
		db = NULL;
		return -1;
	}

	return 0;
}

void db_close(void){
	if(db != NULL){
		sqlite3_close(db);
		db = NULL;
	}
}


