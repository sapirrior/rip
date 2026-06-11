CC      = gcc
VERSION ?= v1.0.0
CFLAGS  = -Wall -Wextra -Iinclude -O2 -std=c99 -D_GNU_SOURCE -DSOME_VERSION=\"$(VERSION)\"
LDFLAGS =

# Core sources
SRC = src/main.c src/terminal.c src/input.c src/pager.c src/ast.c $(wildcard src/gen/*.c)
OBJ    = $(SRC:.c=.o)
TARGET = some

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Generic rule: every .c depends on some.h and ast.h
%.o: %.c include/some.h include/ast.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
