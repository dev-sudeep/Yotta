# Yotta Makefile

CC      = gcc
TARGET  = yotta
SRCDIR  = src
OBJDIR  = build

SRCS = \
	$(SRCDIR)/main.c     \
	$(SRCDIR)/ui.c       \
	$(SRCDIR)/input.c    \
	$(SRCDIR)/editor.c   \
	$(SRCDIR)/highlight.c \
	$(SRCDIR)/explorer.c \
	$(SRCDIR)/git.c      \
	$(SRCDIR)/terminal.c \
	$(SRCDIR)/lsp.c      \
	$(SRCDIR)/chat.c

OBJS = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

# Detect OS for PTY library
UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
	PTY_LIBS = -lutil
else ifeq ($(UNAME), Darwin)
	PTY_LIBS =
else
	PTY_LIBS = -lutil
endif

CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic \
          -Wno-unused-parameter \
          -D_XOPEN_SOURCE=700 \
          -D_DEFAULT_SOURCE \
          -O2 -g
LDFLAGS = $(PTY_LIBS)

.PHONY: all clean install uninstall

all: $(OBJDIR) $(TARGET)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built $(TARGET) successfully."

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"

uninstall:
	rm -f /usr/local/bin/$(TARGET)
	@echo "Uninstalled $(TARGET)"
