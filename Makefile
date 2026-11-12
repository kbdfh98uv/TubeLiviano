PREFIX ?= i686-w64-mingw32-
CC = $(PREFIX)gcc
WINDRES = $(PREFIX)windres
# Native C/x86 has no C++ unwinding; preserve XP SEH while omitting GNU C-frame
# unwind descriptions. LTO removes duplicate code without packing the executable.
CFLAGS = -std=c99 -D__USE_MINGW_ANSI_STDIO=0 -Os -march=i686 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -Wall -Wextra -Werror -ffunction-sections -flto -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS = -mwindows -static-libgcc -Wl,--gc-sections,--no-insert-timestamp,--major-os-version,5,--minor-os-version,1,--major-subsystem-version,5,--minor-subsystem-version,1 -s
LDLIBS = -luser32 -lgdi32 -lshell32
SOURCES = src/main.c src/player.c src/engine.c src/os_detect.c src/settings.c src/ui.c src/stream_cache.c src/engine_worker.c src/engine_process.c
HEADERS = src/player.h src/engine.h src/os_detect.h src/settings.h src/ui.h src/stream_cache.h src/engine_worker.h src/engine_process.h

.PHONY: all clean clips deps package
all: build/portable/TubeLiviano.exe
build/portable/TubeLiviano.exe: $(SOURCES) $(HEADERS) build/app.o
	mkdir -p build/portable
	$(CC) $(CFLAGS) $(SOURCES) build/app.o $(LDFLAGS) $(LDLIBS) -o $@
build/app.o: src/app.rc src/app.manifest
	mkdir -p build
	$(WINDRES) $< $@
clips:
	python3 scripts/generate-clips.py
deps:
	python3 scripts/fetch-deps.py --sources
package: all clips
	python3 scripts/package.py --slim-vlc
clean:
	rm -f build/app.o build/portable/TubeLiviano.exe
