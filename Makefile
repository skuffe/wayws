CC = zig cc
CFLAGS = -O2
PKGCFG = pkg-config

CLIENT_H = ext_workspace_client.h
CLIENT_C = ext_workspace_client.c

WAYWS_SRC = wayws.c
WAYWS_OBJ = $(WAYWS_SRC:.c=.o)

WAYLAND_PROTOCOLS_DIR = /usr/share/wayland-protocols
EXT_WORKSPACE_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/staging/ext-workspace/ext-workspace-v1.xml

WAYLAND_CFLAGS := $(shell $(PKGCFG) --cflags wayland-client)
WAYLAND_LIBS := $(shell $(PKGCFG) --libs wayland-client)

TARGET = wayws

.PHONY: all clean install format lint check

all: $(TARGET)

check: format lint

format:
	clang-format -i $(WAYWS_SRC)

lint: $(CLIENT_H)
	clang-tidy $(WAYWS_SRC) -- $(CFLAGS) $(WAYLAND_CFLAGS)

ext-workspace-v1.xml: $(EXT_WORKSPACE_PROTOCOL)
	cp $< $@

$(CLIENT_H): ext-workspace-v1.xml
	wayland-scanner client-header $< $@

$(CLIENT_C): ext-workspace-v1.xml
	wayland-scanner private-code $< $@

$(WAYWS_OBJ): $(WAYWS_SRC) $(CLIENT_H)

$(TARGET): $(WAYWS_OBJ) $(CLIENT_C)
	$(CC) $(CFLAGS) $(WAYLAND_CFLAGS) -o $@ $^ $(WAYLAND_LIBS)

install:
	sudo install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)

clean:
	rm -f $(TARGET) $(WAYWS_OBJ) $(CLIENT_H) $(CLIENT_C)