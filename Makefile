CC = gcc
CFLAGS = -O2 -fdata-sections -ffunction-sections -pedantic-errors -std=gnu11 -march=native -mtune=native -Wall -Wextra -fstack-protector-strong -fPIE
LIBS = $(shell pkg-config --cflags --libs wayland-client cairo pango pangocairo xkbcommon) -Wl,--gc-sections,-z,relro,-z,now

# Dynamische Pfade zu den Protokollen
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

# Alle C-Dateien, die händisch oder generiert im src-Ordner liegen
SRC = src/buffer.c src/main.c src/wayland-core.c src/window.c src/parser.c src/wlr-layer-shell-unstable-v1.c src/xdg-shell.c
GEN_FILES = src/wlr-layer-shell-unstable-v1-client-protocol.h src/wlr-layer-shell-unstable-v1.c src/xdg-shell-client-protocol.h src/xdg-shell.c

all: $(GEN_FILES) $(TARGET)

$(GEN_FILES):
	@mkdir -p src
	wayland-scanner client-header $(LAYER_SHELL_XML) src/wlr-layer-shell-unstable-v1-client-protocol.h
	wayland-scanner private-code $(LAYER_SHELL_XML) src/wlr-layer-shell-unstable-v1.c
	wayland-scanner client-header $(XDG_SHELL_XML) src/xdg-shell-client-protocol.h
	wayland-scanner private-code $(XDG_SHELL_XML) src/xdg-shell.c

$(TARGET): $(GEN_FILES)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET) src/wlr-layer-shell-unstable-v1.* src/xdg-shell.*

.PHONY: all clean

install: $(TARGET)
	cp $(TARGET) /usr/bin/$(TARGET)
	chmod go+rx /usr/bin/$(TARGET)
