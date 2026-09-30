
# Userland Makefile
CC = gcc
CXX = clang++
RUNTIME_CC = clang --target=x86_64-unknown-none-elf
LD = ld
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -m64 -march=x86-64 -fno-stack-protector -fno-pic -fno-pie -Ilibc/include -Ilibgui/include -Igui/freetype/include -DFT2_BUILD_LIBRARY
LDFLAGS = -static -nostdlib

LIBC_DIR = libc
LIBC = $(LIBC_DIR)/libc.a
LIBM = libm/libm.a
RUNTIME_LIBS = --start-group $(LIBC) $(LIBM) --end-group
CRT0 = $(LIBC_DIR)/crt0.o

# Sources GUI de base
GUI_SRC = gui/render.c gui/font.c gui/fonts/roboto.c gui/freetype_wrapper.c gui/fonts/system_font.c gui/compositor.c gui/wm.c gui/menubar.c gui/dock.c gui/events.c gui/gui.c gui/batch_render.c gui/hw_accel.c gui/vsync.c

# Sources des composants UI
COMPONENTS_SRC = gui/components/component.c gui/components/label.c gui/components/button.c gui/components/panel.c

# Sources FreeType - Base (essentiel)
FREETYPE_BASE = gui/freetype/src/base/ftbase.c gui/freetype/src/base/ftinit.c gui/freetype/src/base/ftsystem.c gui/freetype/src/base/ftdebug.c gui/freetype/src/base/ftbbox.c gui/freetype/src/base/ftbitmap.c gui/freetype/src/base/ftglyph.c gui/freetype/src/base/ftmm.c gui/freetype/src/gzip/ftgzip.c

# Sources FreeType - TrueType driver
FREETYPE_TRUETYPE = gui/freetype/src/truetype/truetype.c

# Sources FreeType - Smooth renderer
FREETYPE_SMOOTH = gui/freetype/src/smooth/smooth.c

# Sources FreeType - Autofit
FREETYPE_AUTOFIT = gui/freetype/src/autofit/autofit.c

# Sources FreeType - PS Names
FREETYPE_PSNAMES = gui/freetype/src/psnames/psnames.c
FREETYPE_PSAUX = gui/freetype/src/psaux/psaux.c
FREETYPE_PSHINTER = gui/freetype/src/pshinter/pshinter.c

# Sources FreeType - SFNT (tables TTF)
FREETYPE_SFNT = gui/freetype/src/sfnt/sfnt.c

# Sources FreeType - Raster (pour le rendu)
FREETYPE_RASTER = gui/freetype/src/raster/raster.c

# Toutes les sources FreeType
FREETYPE_SRC = $(FREETYPE_BASE) $(FREETYPE_TRUETYPE) $(FREETYPE_SMOOTH) $(FREETYPE_AUTOFIT) $(FREETYPE_PSNAMES) $(FREETYPE_PSAUX) $(FREETYPE_PSHINTER) $(FREETYPE_SFNT) $(FREETYPE_RASTER)

# Tous les objets GUI
GUI_OBJ = $(GUI_SRC:.c=.o) $(COMPONENTS_SRC:.c=.o) $(FREETYPE_SRC:.c=.o)

# Liste des utilitaires dans cmd/
CMDS = sh echo pwd cd cat ls mkdir touch rm rmdir meminfo ps ping wget httpd
BIN_DIR = bin
CMD_BINS = $(addprefix $(BIN_DIR)/, $(CMDS))

LIBGUI_OBJ = libgui/gui.o libgui/draw.o
LIBGUI = libgui/libgui.a
DESKTOP_OBJ = desktop/main.o desktop/render.o desktop/wm.o desktop/launcher.o gui/fonts/roboto.o

all: libc_build $(LIBM) libm_build threads-test crash-test fork-test exec-test mmap-test simd-context-test tls-test time-test pthread-test pthread-rwlock-test pthread-attr-test thread-id-test calendar-test libc-common-test fd-io-test fs-metadata-test strtod-test printf-test wide-format-test crt-cxx-test tls-cxx-test gui_app gui-test desktop_app gui-demo sh ls cat mkdir touch rm rmdir meminfo $(CMD_BINS)

# Force libc build
all: env-test uname-test
all: posix-file-test pipe-test socket-test inet-test entropy-test
all: stdio-file-test positional-io-test
all: path-match-test
all: mincore-test
all: system-info-test
all: vector-io-test shared-memory-test resource-test native-compat-test
all: fs-attributes-test
all: msync-test
all: process-control-test
all: path-ops-test
all: stack-protector-test stack-protector-driver
all: base-math-test
all: unix-socket-test
all: int128-runtime-test

unix-socket-test int128-runtime-test: %: %.o $(CRT0) $(LIBC) $(LIBM)
	$(LD) -T linker.ld -o $@ $(CRT0) $< $(RUNTIME_LIBS)

base-math-test.o: base-math-test.c
	$(CC) $(CFLAGS) -fno-builtin -c $< -o $@

stack-protector-test.o: stack-protector-test.c
	$(RUNTIME_CC) $(CFLAGS) -fstack-protector-all -c $< -o $@

stack-protector-test stack-protector-driver base-math-test: %: %.o $(CRT0) $(LIBC) $(LIBM)
	$(LD) -T linker.ld -o $@ $(CRT0) $< $(RUNTIME_LIBS)

posix-file-test pipe-test socket-test inet-test entropy-test stdio-file-test positional-io-test path-match-test mincore-test system-info-test vector-io-test shared-memory-test resource-test native-compat-test fs-attributes-test msync-test process-control-test path-ops-test: %: %.o $(CRT0) $(LIBC) $(LIBM)
	$(LD) -T linker.ld -o $@ $(CRT0) $< $(RUNTIME_LIBS)

env-test: env-test.o $(CRT0) $(LIBC) $(LIBM)
	$(LD) -T linker.ld -o $@ $(CRT0) $< $(RUNTIME_LIBS)

uname-test: uname-test.o $(CRT0) $(LIBC) $(LIBM)
	$(LD) -T linker.ld -o $@ $(CRT0) $< $(RUNTIME_LIBS)

.PHONY: libc_build

libc_build:
	$(MAKE) -C $(LIBC_DIR)

libm_build:
	$(MAKE) -C libm

$(LIBM): libm_build
	@test -f $@

.PHONY: libm_build

$(LIBC) $(CRT0): libc_build
	@test -f $@

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/%: cmd/%.o $(CRT0) $(LIBC) $(LIBM) | $(BIN_DIR)
	$(LD) -T linker.ld -o $@ $(CRT0) $< $(RUNTIME_LIBS)

threads-test: libc_build $(LIBM) threads-test.o
	$(LD) -T linker.ld -o threads-test $(CRT0) threads-test.o $(RUNTIME_LIBS)

crash-test: libc_build $(LIBM) crash-test.o
	$(LD) -T linker.ld -o crash-test $(CRT0) crash-test.o $(RUNTIME_LIBS)

fork-test: libc_build $(LIBM) fork-test.o
	$(LD) -T linker.ld -o fork-test $(CRT0) fork-test.o $(RUNTIME_LIBS)

exec-test: libc_build $(LIBM) exec-test.o
	$(LD) -T linker.ld -o exec-test $(CRT0) exec-test.o $(RUNTIME_LIBS)

mmap-test: libc_build $(LIBM) mmap-test.o
	$(LD) -T linker.ld -o mmap-test $(CRT0) mmap-test.o $(RUNTIME_LIBS)

simd-context-test tls-test time-test pthread-test pthread-rwlock-test pthread-attr-test thread-id-test calendar-test libc-common-test fd-io-test fs-metadata-test strtod-test printf-test wide-format-test: %: %.o $(CRT0) $(LIBC) $(LIBM)
	$(LD) -T linker.ld -o $@ $(CRT0) $< $(RUNTIME_LIBS)

crt-cxx-test tls-cxx-test: %: %.o $(CRT0) $(LIBC) $(LIBM)
	$(LD) -T linker.ld -o $@ $(CRT0) $< $(RUNTIME_LIBS)

crt-cxx-test.o tls-cxx-test.o: %.o: %.cc
	$(CXX) --target=x86_64-unknown-none-elf -std=c++17 -fhosted -fno-builtin -O2 -m64 -fno-pic -fno-pie -fno-stack-protector -fno-exceptions -fno-rtti -ftls-model=local-exec -Ilibc/include -c $< -o $@

simd-context-test.o tls-test.o time-test.o pthread-test.o: %.o: %.c runtime-test.h
	$(RUNTIME_CC) $(CFLAGS) -ftls-model=local-exec -c $< -o $@

gui_app: libc_build $(LIBM) $(GUI_OBJ)
	$(LD) -T linker.ld -o gui_app $(CRT0) $(GUI_OBJ) $(RUNTIME_LIBS)

sh: libc_build $(LIBM) sh.o
	$(LD) -T linker.ld -o sh $(CRT0) sh.o $(RUNTIME_LIBS)

ls: libc_build $(LIBM) ls.o
	$(LD) -T linker.ld -o ls $(CRT0) ls.o $(RUNTIME_LIBS)

cat: libc_build $(LIBM) cat.o
	$(LD) -T linker.ld -o cat $(CRT0) cat.o $(RUNTIME_LIBS)

mkdir: libc_build $(LIBM) mkdir.o
	$(LD) -T linker.ld -o mkdir $(CRT0) mkdir.o $(RUNTIME_LIBS)

touch: libc_build $(LIBM) touch.o
	$(LD) -T linker.ld -o touch $(CRT0) touch.o $(RUNTIME_LIBS)

rm: libc_build $(LIBM) rm.o
	$(LD) -T linker.ld -o rm $(CRT0) rm.o $(RUNTIME_LIBS)

rmdir: libc_build $(LIBM) rmdir.o
	$(LD) -T linker.ld -o rmdir $(CRT0) rmdir.o $(RUNTIME_LIBS)

meminfo: libc_build $(LIBM) meminfo.o
	$(LD) -T linker.ld -o meminfo $(CRT0) meminfo.o $(RUNTIME_LIBS)

gui-test: libc_build $(LIBM) $(GUI_OBJ)
	$(LD) -T linker.ld -o gui-test $(CRT0) $(GUI_OBJ) $(RUNTIME_LIBS)

$(LIBGUI): $(LIBGUI_OBJ)
	ar rcs $@ $^

desktop_app: libc_build $(LIBM) $(DESKTOP_OBJ)
	$(LD) -T linker.ld -o desktop_app $(CRT0) $(DESKTOP_OBJ) $(RUNTIME_LIBS)

gui-demo: libc_build $(LIBM) gui-demo.o $(LIBGUI)
	$(LD) -T linker.ld -o gui-demo $(CRT0) gui-demo.o $(LIBGUI) $(RUNTIME_LIBS)

cmd/%.o: cmd/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(MAKE) -C libm clean
	rm -f env-test uname-test
	rm -f posix-file-test pipe-test socket-test inet-test entropy-test
	rm -f stdio-file-test
	rm -f positional-io-test
	rm -f path-match-test
	rm -f mincore-test
	rm -f system-info-test
	rm -f vector-io-test shared-memory-test resource-test native-compat-test
	rm -f fs-attributes-test
	rm -f msync-test
	rm -f process-control-test
	rm -f path-ops-test
	rm -f stack-protector-test stack-protector-driver
	rm -f base-math-test
	rm -f unix-socket-test
	rm -f int128-runtime-test
	rm -f simd-context-test tls-test time-test pthread-test pthread-rwlock-test pthread-attr-test thread-id-test calendar-test libc-common-test fd-io-test fs-metadata-test strtod-test printf-test wide-format-test crt-cxx-test tls-cxx-test
	rm -rf bin cmd/*.o *.o threads-test crash-test fork-test exec-test mmap-test gui_app gui/*.o gui/fonts/*.o gui/components/*.o
	rm -f sh ls cat mkdir touch rm rmdir meminfo gui-test desktop_app gui-demo desktop/*.o libgui/*.o libgui/libgui.a
	find gui/freetype/src -name "*.o" -delete
	$(MAKE) -C $(LIBC_DIR) clean

.PHONY: all clean
