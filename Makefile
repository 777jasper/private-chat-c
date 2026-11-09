CC	= gcc
CFLAGS	= -Wall -Wextra -g -pthread -Isrc/common
LDFLAGS	= -pthread
SODIUM_CFLAGS := $(shell pkg-config --cflags libsodium)
SODIUM_LIBS := $(shell pkg-config --libs libsodium)



LDLIBS	= -ldl -lm $(SODIUM_LIBS)



BUILD	= build

SERVER_OBJS = $(BUILD)/server.o $(BUILD)/net_io.o $(BUILD)/db.o $(BUILD)/auth.o

SQLITE_DIR = deps/sqlite/sqlite-amalgamation-3540100
SQLITE_OBJ = $(BUILD)/sqlite3.o


all: $(BUILD)/server

$(BUILD)/server: $(SERVER_OBJS) $(SQLITE_OBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/server.o: src/server/server.c src/common/net_io.h src/common/protocol.h src/server/db.h src/server/auth.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/net_io.o: src/common/net_io.c src/common/net_io.h src/common/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/db.o: src/server/db.c src/server/db.h $(SQLITE_DIR)/sqlite3.h
	$(CC) $(CFLAGS) -I$(SQLITE_DIR) -c $< -o $@

$(BUILD)/auth.o: src/server/auth.c src/server/auth.h
	$(CC) $(CFLAGS) $(SODIUM_CFLAGS) -c $< -o $@


$(SQLITE_OBJ): $(SQLITE_DIR)/sqlite3.c $(SQLITE_DIR)/sqlite3.h
	$(CC) -O2 -pthread -DSQLITE_THREADSAFE=1 -c $< -o $@


sqlite: $(SQLITE_OBJ)


clean:
	rm -f $(BUILD)/server $(SERVER_OBJS)

clean-all: 
	rm -f $(BUILD)/server $(BUILD)/*.o

.PHONY: all clean clean-all sqlite
