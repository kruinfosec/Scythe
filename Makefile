CC = gcc
CFLAGS = -Wall `pkg-config --cflags gtk+-3.0 vte-2.91 json-glib-1.0 libsoup-3.0 webkit2gtk-4.1`
LIBS = `pkg-config --libs gtk+-3.0 vte-2.91 json-glib-1.0 libsoup-3.0 webkit2gtk-4.1` -luuid -lhpdf -lsqlite3 -lX11

SRC_DIR = src
BIN_DIR = bin
OBJ_DIR = obj

SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/config.c \
       $(SRC_DIR)/ai.c \
       $(SRC_DIR)/terminal.c \
       $(SRC_DIR)/scripts.c \
       $(SRC_DIR)/utils.c \
       $(SRC_DIR)/conversation.c \
       $(SRC_DIR)/webview_ai.c \
       $(SRC_DIR)/db.c \
       $(SRC_DIR)/theme_manager.c \
       $(SRC_DIR)/notes.c

OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
TARGET = $(BIN_DIR)/scythe-fixed

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) -o $@ $^ $(LIBS)

$(OBJ_DIR)/webview_ai.o: $(SRC_DIR)/webview_ai.c $(SRC_DIR)/webview_ai.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/db.o: $(SRC_DIR)/db.c $(SRC_DIR)/db.h
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
