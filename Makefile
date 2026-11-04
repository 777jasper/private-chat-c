CC	= gcc
CFLAGS	= -Wall -Wextra -g -pthread -Isrc/common
LDFLAGS	= -pthread

BUILD	= build

SERVER_OBJS = $(BUILD)/server.o $(BUILD)/net_io.o

all: $(BUILD)/server

$(BUILD)/server: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/server.o: src/server/server.c src/common/net_io.h src/common/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/net_io.o: src/common/net_io.c src/common/net_io.h src/common/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(BUILD)/server $(BUILD)/*.o

.PHONY: all clean

