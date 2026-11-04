CC	= gcc
CFLAGS	= -Wall -Wextra -g -pthread -Isrc/common
LDFLAGS	= -pthread

BUILD	= build

SERVER_OBJS = $(BUILD)/server.o $(BUILD)/net_io.o

SQLITE_DIR = deps/sqlite/sqlite-amalgamation-3540100
SQLITE_OBJ = $(BUILD)/sqlite3.o


all: $(BUILD)/server

$(BUILD)/server: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/server.o: src/server/server.c src/common/net_io.h src/common/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/net_io.o: src/common/net_io.c src/common/net_io.h src/common/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SQLITE_OBJ): $(SQLITE_DIR)/sqlite3.c $(SQLITE_DIR)/sqlite3.h
	$(CC) -O2 -pthread -DSQLITE_THREADSAFE=1 -c $< -o $@

sqlite: $(SQLITE_OBJ)


clean:
	rm -f $(BUILD)/server $(SERVER_OBJS)

clean-all: 
	rm -f $(BUILD)/server $(BUILD)/*.o

.PHONY: all clean sqlite
