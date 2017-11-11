CC = gcc

DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CFLAGS = -D DEBUG -g -O1 -Wall -Wextra -pedantic -std=c99
	TARGET = target/debug
else
	CFLAGS = -O2 -Wall -Wextra -pedantic -std=c99
	TARGET = target/release
endif

PROJECT = rip-stream-server

SRCS = $(shell find src -name '*.c')
DIRS = $(shell find src -type d | sed 's/src/./g' )
OBJS = $(patsubst src/%.f,${TARGET}/%.o,$(SRCS))

${TARGET}/$(PROJECT): buildrepo $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

${TARGET}/%.o: src/%.f
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf target

buildrepo:
	for dir in $(DIRS); do mkdir -p ${TARGET}/$$dir; done

