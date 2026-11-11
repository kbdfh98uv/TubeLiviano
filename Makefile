PREFIX ?= i686-w64-mingw32-
CC = $(PREFIX)gcc
WINDRES = $(PREFIX)windres
CFLAGS = -std=c99 -D__USE_MINGW_ANSI_STDIO=0 -Os -march=i686 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -Wall -Wextra -Werror -ffunction-sections
LDFLAGS = -mwindows -static-libgcc -Wl,--gc-sections,--no-insert-timestamp,--major-os-version,5,--minor-os-version,1,--major-subsystem-version,5,--minor-subsystem-version,1 -s
LDLIBS = -luser32 -lgdi32 -lshell32

.PHONY: all clean clips deps package
all: build/portable/TubeLiviano.exe
build/portable/TubeLiviano.exe: src/main.c src/player.c src/engine.c src/os_detect.c src/player.h src/engine.h src/os_detect.h build/app.o
	mkdir -p build/portable
	$(CC) $(CFLAGS) src/main.c src/player.c src/engine.c src/os_detect.c build/app.o $(LDFLAGS) $(LDLIBS) -o $@
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
