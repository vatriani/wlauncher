CC = gcc
CFLAGS = -O2 -march=native -mtune=native -Wall -Wextra
LIBS = $(shell pkg-config --cflags --libs wayland-client cairo pango)

ifneq ($(shell pkg-config --exists wlr-protocols && echo yes),yes)
    PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
    LAYER_SHELL_XML = $(PROTOCOLS_DIR)/ext/wlr-layer-shell-v1.xml
else
    PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wlr-protocols)
    LAYER_SHELL_XML = $(PROTOCOLS_DIR)/unstable/wlr-layer-shell-unstable-v1.xml
endif

WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

TARGET = wlauncher
SRC = src/main.c src/wlr-layer-shell-unstable-v1.c src/xdg-shell.c
GEN_HEADERS = src/wlr-layer-shell-unstable-v1-client-protocol.h src/xdg-shell-client-protocol.h

all: $(GEN_HEADERS) $(TARGET)

$(GEN_HEADERS):
	@mkdir -p src
	wayland-scanner client-header $(LAYER_SHELL_XML) src/wlr-layer-shell-unstable-v1-client-protocol.h
	wayland-scanner private-code $(LAYER_SHELL_XML) src/wlr-layer-shell-unstable-v1.c
	wayland-scanner client-header $(XDG_SHELL_XML) src/xdg-shell-client-protocol.h
	wayland-scanner private-code $(XDG_SHELL_XML) src/xdg-shell.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS) -lrt

run:
	./wlauncher

clean:
	rm -f *.d *.o $(TARGET)

.PHONY: all clean

install: $(TARGET)
	cp $(TARGET) /usr/bin/$(TARGET)
	chmod go+rx /usr/bin/$(TARGET)
