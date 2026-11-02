CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2
SANITIZERS = -fsanitize=address,undefined -fno-omit-frame-pointer
CORE = src/core/alarm.c src/core/audit.c src/core/sha256.c
HEADERS = src/core/alarm.h src/core/audit.h src/core/sha256.h

.PHONY: test

test: build/test_core
	./build/test_core

build/test_core: tests/test_core.c $(CORE) $(HEADERS) Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZERS) -Isrc/core tests/test_core.c $(CORE) -o $@
