CC      := gcc
CFLAGS  := -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
OBJ_DIR := build
BIN_DIR := bin

SRC_COMMON      := src/common/mc_protocol.c
SRC_SERVER      := src/server/mc_server.c src/server/main.c
SRC_PROTOCOL_T  := tests/protocol_demo.c
SRC_SMOKE_CLIENT:= tests/smoke_client.c

COMMON_OBJS := $(OBJ_DIR)/mc_protocol.o
SERVER_OBJS := $(COMMON_OBJS) $(OBJ_DIR)/mc_server.o $(OBJ_DIR)/server_main.o
PROTO_OBJS  := $(COMMON_OBJS) $(OBJ_DIR)/protocol_demo.o
SMOKE_OBJS  := $(COMMON_OBJS) $(OBJ_DIR)/smoke_client.o

.PHONY: all clean test-protocol test-server server

all: test-protocol

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(OBJ_DIR)/mc_protocol.o: src/common/mc_protocol.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/mc_server.o: src/server/mc_server.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/server_main.o: src/server/main.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/protocol_demo.o: tests/protocol_demo.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/smoke_client.o: tests/smoke_client.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/protocol_demo: $(PROTO_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/server: $(SERVER_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/smoke_client: $(SMOKE_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

test-protocol: $(BIN_DIR)/protocol_demo
	./$(BIN_DIR)/protocol_demo

server: $(BIN_DIR)/server

test-server: $(BIN_DIR)/server $(BIN_DIR)/smoke_client
	@bash -c 'set -euo pipefail; \
	PORT=9400; \
	LOG=$$(mktemp -t mc-server-log.XXXXXX); \
	./$(BIN_DIR)/server $$PORT > $$LOG 2>&1 & \
	SERVER_PID=$$!; \
	sleep 1; \
	./$(BIN_DIR)/smoke_client 127.0.0.1 $$PORT; \
	kill -INT $$SERVER_PID 2>/dev/null || true; \
	wait $$SERVER_PID 2>/dev/null || true; \
	echo "Server smoke test log (port $$PORT):"; \
	cat $$LOG; \
	rm -f $$LOG'

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
