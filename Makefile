CC	= gcc
CFLAGS	= -Wall -Wextra -g -pthread -Isrc/common
LDFLAGS	= -pthread
SODIUM_CFLAGS := $(shell pkg-config --cflags libsodium)
SODIUM_LIBS := $(shell pkg-config --libs libsodium)



LDLIBS	= -ldl -lm $(SODIUM_LIBS)



BUILD	= build

SERVER_OBJS = $(BUILD)/server.o $(BUILD)/net_io.o $(BUILD)/db.o $(BUILD)/auth.o $(BUILD)/registry.o $(BUILD)/conn.o
CLIENT_OBJS = $(BUILD)/client.o $(BUILD)/net_io.o $(BUILD)/keys.o

SQLITE_DIR = deps/sqlite/sqlite-amalgamation-3540100
SQLITE_OBJ = $(BUILD)/sqlite3.o


all: $(BUILD)/server $(BUILD)/client

$(BUILD)/server: $(SERVER_OBJS) $(SQLITE_OBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/client: $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) $^ $(SODIUM_LIBS) -o $@

$(BUILD)/server.o: src/server/server.c src/common/net_io.h src/common/protocol.h src/server/db.h src/server/auth.h src/server/conn.h src/server/registry.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/net_io.o: src/common/net_io.c src/common/net_io.h src/common/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/db.o: src/server/db.c src/server/db.h $(SQLITE_DIR)/sqlite3.h
	$(CC) $(CFLAGS) -I$(SQLITE_DIR) -c $< -o $@

$(BUILD)/auth.o: src/server/auth.c src/server/auth.h
	$(CC) $(CFLAGS) $(SODIUM_CFLAGS) -c $< -o $@

$(BUILD)/registry.o: src/server/registry.c src/server/registry.h src/server/conn.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/conn.o: src/server/conn.c src/server/conn.h src/common/net_io.h src/common/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/client.o: src/client/client.c src/common/net_io.h src/common/protocol.h src/client/keys.h
	$(CC) $(CFLAGS) $(SODIUM_CFLAGS) -c $< -o $@

$(BUILD)/keys.o: src/client/keys.c src/client/keys.h
	$(CC) $(CFLAGS) $(SODIUM_CFLAGS) -c $< -o $@


$(SQLITE_OBJ): $(SQLITE_DIR)/sqlite3.c $(SQLITE_DIR)/sqlite3.h
	$(CC) -O2 -pthread -DSQLITE_THREADSAFE=1 -c $< -o $@


sqlite: $(SQLITE_OBJ)


clean:
	rm -f $(BUILD)/server $(SERVER_OBJS) $(BUILD)/client $(BUILD)/client.o $(BUILD)/keys.o

clean-all: 
	rm -f $(BUILD)/server $(BUILD)/*.o

.PHONY: all clean clean-all sqlite
