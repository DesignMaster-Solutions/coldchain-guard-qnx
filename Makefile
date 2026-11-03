CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2
SANITIZERS = -fsanitize=address,undefined -fno-omit-frame-pointer
CORE = src/core/alarm.c src/core/audit.c src/core/sha256.c
HEADERS = src/core/alarm.h src/core/audit.h src/core/sha256.h

.PHONY: test

test: build/test_core build/test_fridge build/test_storage build/test_forecast build/test_fgctl build/fgctl build/replay_target
	./build/test_core
	./build/test_fridge
	./build/test_storage
	./build/test_forecast
	./build/test_fgctl
	python3 tests/test_serial.py

build/test_core: tests/test_core.c $(CORE) $(HEADERS) Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZERS) -Isrc/core tests/test_core.c $(CORE) -o $@

build/test_fridge: tests/test_fridge.c src/fridged/portable.c src/fridged/portable.h src/core/forecast.h $(CORE) $(HEADERS) Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZERS) -Isrc/core -Isrc/fridged tests/test_fridge.c src/fridged/portable.c $(CORE) -o $@

# M4 POSIX storage integration, independent of the portable core tests.
build/test_storage: tests/test_storage.c src/fridged/storage.c src/fridged/storage.h $(CORE) $(HEADERS) Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZERS) -Isrc/core -Isrc/fridged tests/test_storage.c src/fridged/storage.c $(CORE) -o $@

build/test_forecast: tests/test_forecast.c src/core/forecast.c src/core/forecast.h Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZERS) -Isrc/core tests/test_forecast.c src/core/forecast.c -o $@

FGCTL = src/fgctl/fgctl.c src/fridged/portable.c $(CORE)
FGCTL_HEADERS = src/core/forecast.h src/fgctl/fgctl.h src/fridged/portable.h src/fridged/storage.h $(HEADERS)

build/fgctl: src/fgctl/main.c $(FGCTL) $(FGCTL_HEADERS) Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZERS) -Isrc/core -Isrc/fridged -Isrc/fgctl src/fgctl/main.c $(FGCTL) -o $@

build/test_fgctl: tests/test_fgctl.c $(FGCTL) $(FGCTL_HEADERS) Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZERS) -Isrc/core -Isrc/fridged -Isrc/fgctl tests/test_fgctl.c $(FGCTL) -o $@

build/replay_target: tools/host/replay-target.c src/fridged/portable.c src/fridged/storage.c src/core/forecast.c src/core/forecast.h $(FGCTL_HEADERS) $(CORE) Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(SANITIZERS) -Isrc/core -Isrc/fridged tools/host/replay-target.c src/fridged/portable.c src/fridged/storage.c src/core/forecast.c $(CORE) -o $@
