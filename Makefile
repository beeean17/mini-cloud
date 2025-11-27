CC      := gcc
CFLAGS  := -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
OBJ_DIR := build
BIN_DIR := bin

SRC_COMMON      := src/common/mc_protocol.c
SRC_SERVER      := src/server/mc_server.c src/server/main.c
SRC_CLIENT      := src/client/mc_client.c src/client/main.c
SRC_PROTOCOL_T  := tests/protocol_demo.c
SRC_SMOKE_CLIENT:= tests/smoke_client.c

COMMON_OBJS := $(OBJ_DIR)/mc_protocol.o
SERVER_OBJS := $(COMMON_OBJS) $(OBJ_DIR)/mc_server.o $(OBJ_DIR)/server_main.o
PROTO_OBJS  := $(COMMON_OBJS) $(OBJ_DIR)/protocol_demo.o
SMOKE_OBJS  := $(COMMON_OBJS) $(OBJ_DIR)/smoke_client.o
CLIENT_OBJS := $(COMMON_OBJS) $(OBJ_DIR)/mc_client.o $(OBJ_DIR)/client_main.o

.PHONY: all clean test-protocol test-server test-client test-stress server client

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

$(OBJ_DIR)/mc_client.o: src/client/mc_client.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/client_main.o: src/client/main.c | $(OBJ_DIR)
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

$(BIN_DIR)/client: $(CLIENT_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

test-protocol: $(BIN_DIR)/protocol_demo
	./$(BIN_DIR)/protocol_demo

server: $(BIN_DIR)/server

client: $(BIN_DIR)/client

test-server: $(BIN_DIR)/server $(BIN_DIR)/smoke_client
	@bash -c 'set -euo pipefail; \
	PORT=9400; \
	AUTH_TOKEN="smoke-secret"; \
	LOG=$$(mktemp -t mc-server-log.XXXXXX); \
	MC_SERVER_TOKEN="$$AUTH_TOKEN" ./$(BIN_DIR)/server $$PORT > $$LOG 2>&1 & \
	SERVER_PID=$$!; \
	sleep 1; \
	MC_CLIENT_TOKEN="$$AUTH_TOKEN" ./$(BIN_DIR)/smoke_client 127.0.0.1 $$PORT; \
	kill -INT $$SERVER_PID 2>/dev/null || true; \
	wait $$SERVER_PID 2>/dev/null || true; \
	echo "Server smoke test log (port $$PORT):"; \
	cat $$LOG; \
	rm -f $$LOG'

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

test-client: $(BIN_DIR)/server $(BIN_DIR)/client
	@bash -c 'set -euo pipefail; \
	PORT=9500; \
	AUTH_TOKEN="client-secret"; \
	UPLOAD_LIMIT=1024; \
	TMP_UPLOAD=$$(mktemp -t mc-upload.XXXXXX); \
	echo "hello from client" > $$TMP_UPLOAD; \
	BASENAME=$$(basename $$TMP_UPLOAD); \
	BIG_FILE=$$(mktemp -t mc-big-upload.XXXXXX); \
	head -c 2048 /dev/zero > $$BIG_FILE; \
	MC_SERVER_TOKEN="$$AUTH_TOKEN" MC_MAX_UPLOAD_BYTES="$$UPLOAD_LIMIT" ./$(BIN_DIR)/server $$PORT >/tmp/mc-client-server.log 2>&1 & \
	SERVER_PID=$$!; \
	sleep 1; \
	printf "LIST\nUPLOAD $$TMP_UPLOAD\nDOWNLOAD $$BASENAME\nUPLOAD $$BIG_FILE\nQUIT\n" | \
	MC_CLIENT_TOKEN="$$AUTH_TOKEN" ./$(BIN_DIR)/client 127.0.0.1 $$PORT >/tmp/mc-client.log 2>&1; \
	kill -INT $$SERVER_PID 2>/dev/null || true; \
	wait $$SERVER_PID 2>/dev/null || true; \
	diff -q $$TMP_UPLOAD $$BASENAME >/tmp/mc-diff.log; \
	grep -q "exceeds limit" /tmp/mc-client.log; \
	echo "Client log:"; cat /tmp/mc-client.log; \
	echo "Server log:"; cat /tmp/mc-client-server.log; \
	echo "Diff log:"; cat /tmp/mc-diff.log; \
	rm -f $$TMP_UPLOAD $$BASENAME $$BIG_FILE /tmp/mc-client.log /tmp/mc-client-server.log /tmp/mc-diff.log'

test-stress:
	@tests/multi_client.sh
