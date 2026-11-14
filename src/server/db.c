#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sqlite3.h"
#include <time.h>
#include "db.h"

/* The one shared connection. "static" makes it private to this file. */
static sqlite3 *db = NULL;

/* Adjacent string literals in C are joined into one string. */
static const char *SCHEMA_SQL =
	"CREATE TABLE IF NOT EXISTS users ("
	"id	INTEGER PRIMARY KEY AUTOINCREMENT,"
	"username TEXT NOT NULL UNIQUE,"
	"pw_hash TEXT NOT NULL,"
	"pubkey BLOB"
	");"
	"CREATE TABLE IF NOT EXISTS messages ("
        "id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender_id    INTEGER NOT NULL,"
        "recipient_id INTEGER NOT NULL,"
        "sent_at      INTEGER NOT NULL,"
        "body         BLOB NOT NULL,"
        "delivered    INTEGER NOT NULL DEFAULT 0"
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

int db_add_user(const char *username, const char *pw_hash){

	if(db==NULL)
		return -1;
	const char *sql = "INSERT INTO users (username, pw_hash) VALUES (?, ?); ";
	sqlite3_stmt *stmt = NULL;

	/* compile the SQL. */
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if(rc!=SQLITE_OK){
		fprintf(stderr,"db: prepare failed: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	/* fill in the two placeholders. */
	sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, pw_hash, -1, SQLITE_TRANSIENT);

	/* run */
	rc = sqlite3_step(stmt);

	/* decide the results before finalizing */
	int result;
	if(rc == SQLITE_DONE)
		result =  0;
	else if(rc == SQLITE_CONSTRAINT)
		result =  1;
	else{
		fprintf(stderr, "db: insert failed: %s\n", sqlite3_errmsg(db));
		result = -1;
	}

	sqlite3_finalize(stmt);
	return result;
}

int db_get_hash(const char *username, char *out, size_t out_size){

	if(db == NULL)
		return -1;

	const char *sql = "SELECT pw_hash FROM users WHERE username = ?;";
	sqlite3_stmt *stmt = NULL;

	/* Compile the SQL. */
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if(rc!=SQLITE_OK){
		fprintf(stderr,"db: prepare failed: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	/* bind the username to placeholder. */
	sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);

	rc = sqlite3_step(stmt);
	int result;
	if(rc == SQLITE_ROW){
		const unsigned char *text = sqlite3_column_text(stmt,0);
		if(text == NULL || strlen((const char *)text)>=out_size){
			result = -1;
		}
		else{
			memcpy(out, text, strlen((const char *)text)+1);
			result = 0;
		}
	}
	else if(rc == SQLITE_DONE){
		result = 1;
	}
	else{
		fprintf(stderr, "db: lookup failed: %s\n", sqlite3_errmsg(db));
		result = -1;
	}

	sqlite3_finalize(stmt);
	return result;
}

int db_set_pubkey(const char *username, const unsigned char *key){
	if(db == NULL)
		return -1;

	const char *sql = "UPDATE users SET pubkey = ? WHERE username = ?;";
	sqlite3_stmt *stmt = NULL;

	/* Compile the sql */
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if(rc != SQLITE_OK){
		fprintf(stderr, "db: prepare failed: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	sqlite3_bind_blob(stmt, 1, key, DB_PUBKEY_SIZE, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, username, -1, SQLITE_TRANSIENT);

	rc = sqlite3_step(stmt);
	int result;
	if(rc == SQLITE_DONE)
		result = (sqlite3_changes(db) == 1) ? 0 : 1;
	else{
		fprintf(stderr, "db: update failed: %s\n", sqlite3_errmsg(db));
		result = -1;
	}

	sqlite3_finalize(stmt);
	return result;
}

int db_get_pubkey(const char *username, unsigned char *out){
	if(db == NULL)
		return -1;

	const char *sql = "SELECT pubkey FROM users WHERE username = ?; ";
	sqlite3_stmt *stmt = NULL;

	/* Compile the SQL. */
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if(rc!=SQLITE_OK){
                fprintf(stderr,"db: prepare failed: %s\n", sqlite3_errmsg(db));
                return -1;
        }

	/* bind the username to palceholder 1 */
	sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);

	rc = sqlite3_step(stmt);
	int result;
	if(rc == SQLITE_ROW){
		if(sqlite3_column_type(stmt, 0) == SQLITE_NULL)
			result = 1; /* no key uploaded yet. */
		else{
			const void *blob = sqlite3_column_blob(stmt, 0);
			int n = sqlite3_column_bytes(stmt,0);
			if(blob == NULL || n!= DB_PUBKEY_SIZE)
				result = -1;
			else{
				memcpy(out, blob, DB_PUBKEY_SIZE);
				result = 0;
			}
		}
	}
	else if(rc == SQLITE_DONE)
		result = 1;
	else{
		fprintf(stderr, "db: key lookup failed: %s\n", sqlite3_errmsg(db));
		result = -1;
	}
	sqlite3_finalize(stmt);
	return result;
}

int db_store_message(const char *sender, const char *recipient, const uint8_t *body, size_t body_len, int64_t *id_out){

	if(db == NULL)
		return -1;

	const char *sql =
		"INSERT INTO messages (sender_id, recipient_id, sent_at, body)"
		"SELECT s.id, r.id, ?, ? FROM users s, users r "
		"WHERE s.username = ? AND r.username = ? RETURNING id;";

	sqlite3_stmt *stmt = NULL;

	/* compile the SQL */
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if(rc!= SQLITE_OK){
		fprintf(stderr, "db: prepare failed: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	/* bind the four place holders in the order they appear */
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)time(NULL));
	sqlite3_bind_blob(stmt, 2, body, (int)body_len, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, sender, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, recipient, -1, SQLITE_TRANSIENT);

	rc = sqlite3_step(stmt);
	int result;
	if(rc == SQLITE_ROW){
		if(id_out != NULL)
			*id_out = (int64_t)sqlite3_column_int64(stmt,0);
		rc = sqlite3_step(stmt);
		
		if(rc == SQLITE_DONE)
			result = 0;

		else{
			fprintf(stderr, "db: store failed: %s\n", sqlite3_errmsg(db));
			result = -1;
		}
	}
	else if(rc == SQLITE_DONE){
		result = 1; /* no such user */
	}
	else{
		fprintf(stderr, "db: store failed: %s\n", sqlite3_errmsg(db));
		result = -1;
	}
	sqlite3_finalize(stmt);
	return result;
}

int db_mark_delivered(int64_t id){

	if(db == NULL)
		return -1;

	const char *sql = "UPDATE messages SET delivered = 1 WHERE id = ?;";
	sqlite3_stmt *stmt = NULL;

	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if(rc!=SQLITE_OK){
		fprintf(stderr, "db: prepare failed: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id);

	rc = sqlite3_step(stmt);
	int result = 0;
	if(rc != SQLITE_DONE){
		fprintf(stderr, "db: mark failed: %s\n", sqlite3_errmsg(db));
		result = -1;
	}
	sqlite3_finalize(stmt);
	return result;
}

int db_next_undelivered(const char *username, int64_t *id, char *sender, size_t sender_size, uint8_t **body, size_t *body_len){

	if(db == NULL)
		return -1;
	const char *sql =
		"SELECT m.id, s.username, m.body FROM messages m "
		"JOIN users s ON s.id = m.sender_id "
		"JOIN users r on r.id = m.recipient_id "
		"WHERE r.username = ? AND m.delivered = 0 "
		"ORDER BY m.id LIMIT 1;";
	sqlite3_stmt *stmt = NULL;

	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if(rc!=SQLITE_OK){
                fprintf(stderr, "db: prepare failed: %s\n", sqlite3_errmsg(db));
                return -1;
        }

	sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);

	rc = sqlite3_step(stmt);
	int result;
	if(rc == SQLITE_ROW){
		const unsigned char *name = sqlite3_column_text(stmt, 1);
		const void *blob = sqlite3_column_blob(stmt, 2);
		int n = sqlite3_column_bytes(stmt, 2);

		if(name == NULL || blob == NULL || n <= 0 || strlen((const char *)name)>=sender_size){
			result = -1;
		}
		else{
			uint8_t *copy = malloc((size_t)n);
			if(copy == NULL){
				result = -1;
			}
			else{
				memcpy(copy, blob, (size_t)n);
				*body = copy;
				*body_len = (size_t)n;
				*id = (int64_t)sqlite3_column_int64(stmt, 0);
				memcpy(sender, name, strlen((const char *)name) + 1);
				result = 0;
			}
		}
	}
	else if(rc == SQLITE_DONE)
		result = 1;
	else{
		fprintf(stderr, "db: fetch failed: %s\n", sqlite3_errmsg(db));
		result = -1;
	}
	sqlite3_finalize(stmt);
	return result;
}

int db_history_start(const char *user, const char *peer, int count, int64_t *start){

	if(db == NULL || count <= 0)
		return -1;
	const char *sql =
		"SELECT COALESCE((SELECT m.id FROM messages m "
		"JOIN users a ON a.id = m.sender_id "
		"JOIN users b ON b.id = m.recipient_id "
		"WHERE (a.username = ?1 AND b.username = ?2) "
		"OR (a.username = ?2 AND b.username = ?1) "
		"ORDER BY m.id DESC LIMIT 1 OFFSET ?3), 0);";

	sqlite3_stmt *stmt = NULL;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
        if(rc!=SQLITE_OK){
                fprintf(stderr, "db: prepare failed: %s\n", sqlite3_errmsg(db));
                return -1;
        }

	sqlite3_bind_text(stmt, 1, user, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, peer, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, count);

	rc = sqlite3_step(stmt);
	int result;
	if(rc == SQLITE_ROW){
		*start = (int64_t)sqlite3_column_int64(stmt, 0);
		result = 0;
	}
	else{
		fprintf(stderr, "db: history lookup failed: %s\n", sqlite3_errmsg(db));
		return -1;
	}

	sqlite3_finalize(stmt);
	return result;
}
