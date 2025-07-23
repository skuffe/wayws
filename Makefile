CC = gcc
CFLAGS = -O2
PKGCFG = pkg-config

CLIENT_H = ext_workspace_client.h
CLIENT_C = ext_workspace_client.c

WAYWS_SRC = wayws.c util.c workspace.c wayland.c output.c event.c
WAYWS_OBJ = $(WAYWS_SRC:.c=.o)

WAYLAND_PROTOCOLS_DIR = /usr/share/wayland-protocols
EXT_WORKSPACE_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/staging/ext-workspace/ext-workspace-v1.xml

WAYLAND_CFLAGS := $(shell $(PKGCFG) --cflags wayland-client)
WAYLAND_LIBS := $(shell $(PKGCFG) --libs wayland-client)
CMOCKA_LIBS := $(shell $(PKGCFG) --libs cmocka)

TARGET = wayws
TEST_RUNNER_UTIL = test_runner_util
TEST_RUNNER_WORKSPACE = test_runner_workspace
TEST_RUNNER_EVENT = test_runner_event
TEST_RUNNER_CLI = test_runner_cli

.PHONY: all clean install format lint check test test-unit test-integration

all: $(TARGET)

test: $(TEST_RUNNER_UTIL) $(TEST_RUNNER_WORKSPACE) $(TEST_RUNNER_EVENT) $(TEST_RUNNER_CLI)
	./$(TEST_RUNNER_UTIL)
	./$(TEST_RUNNER_WORKSPACE)
	./$(TEST_RUNNER_EVENT)
	./$(TEST_RUNNER_CLI)
	./tests/test_integration.sh

test-unit: $(TEST_RUNNER_UTIL) $(TEST_RUNNER_WORKSPACE) $(TEST_RUNNER_EVENT) $(TEST_RUNNER_CLI)
	./$(TEST_RUNNER_UTIL)
	./$(TEST_RUNNER_WORKSPACE)
	./$(TEST_RUNNER_EVENT)
	./$(TEST_RUNNER_CLI)

test-integration: $(TARGET)
	./tests/test_integration.sh

check: format lint

format:
	clang-format -i $(WAYWS_SRC) *.h tests/*.c

lint: $(CLIENT_H)
	clang-tidy $(WAYWS_SRC) -- $(CFLAGS) $(WAYLAND_CFLAGS)

ext-workspace-v1.xml: $(EXT_WORKSPACE_PROTOCOL)
	cp $< $@

$(CLIENT_H): ext-workspace-v1.xml
	wayland-scanner client-header $< $@

$(CLIENT_C): ext-workspace-v1.xml
	wayland-scanner private-code $< $@

$(WAYWS_OBJ): $(CLIENT_H)

$(TARGET): $(WAYWS_OBJ) $(CLIENT_C)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -o $@ $^ $(WAYLAND_LIBS)

TEST_CC = gcc
$(TEST_RUNNER_UTIL): tests/test_util.c util.o
	$(TEST_CC) $(CFLAGS) -o $@ $^ $(CMOCKA_LIBS)

$(TEST_RUNNER_WORKSPACE): tests/test_workspace.c workspace.o util.o
	$(TEST_CC) $(CFLAGS) -Wl,--wrap=die -o $@ $^ $(WAYLAND_LIBS) $(CMOCKA_LIBS)

$(TEST_RUNNER_EVENT): tests/test_event.c event.o util.o
	$(TEST_CC) $(CFLAGS) -o $@ $^ $(CMOCKA_LIBS)

$(TEST_RUNNER_CLI): tests/test_cli.c util.o
	$(TEST_CC) $(CFLAGS) -o $@ $^ $(CMOCKA_LIBS)


install:
	sudo install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET) $(WAYWS_OBJ) $(CLIENT_H) $(CLIENT_C) ext-workspace-v1.xml $(TEST_RUNNER_UTIL) $(TEST_RUNNER_WORKSPACE) $(TEST_RUNNER_EVENT) $(TEST_RUNNER_CLI)
