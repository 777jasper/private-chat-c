#ifndef DB_H
#define DB_H

/* Opens the database file at path (creating it if needed) and makes sure the table exist. Returns 0 on success, -1 on failure. */
int db_init(const char *path);

/* Close the database. */
void db_close(void);

#endif
