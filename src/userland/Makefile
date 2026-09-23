
# Userland Makefile
CC = gcc
LD = ld
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -m64 -march=x86-64 -fno-stack-protector -fno-pic -fno-pie -Ilibc/include -Igui/freetype/include -DFT2_BUILD_LIBRARY
LDFLAGS = -static -nostdlib

LIBC_DIR = libc
LIBC = $(LIBC_DIR)/libc.a
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

all: threads-test fork-test exec-test gui_app sh ls cat mkdir touch rm rmdir meminfo


# Force libc build
.PHONY: libc_build

libc_build:
	$(MAKE) -C $(LIBC_DIR)

threads-test: libc_build threads-test.o
	$(LD) -T linker.ld -o threads-test $(CRT0) threads-test.o $(LIBC)

fork-test: libc_build fork-test.o
	$(LD) -T linker.ld -o fork-test $(CRT0) fork-test.o $(LIBC)

exec-test: libc_build exec-test.o
	$(LD) -T linker.ld -o exec-test $(CRT0) exec-test.o $(LIBC)

sh: libc_build sh.o
	$(LD) -T linker.ld -o sh $(CRT0) sh.o $(LIBC)

ls: libc_build ls.o
	$(LD) -T linker.ld -o ls $(CRT0) ls.o $(LIBC)

cat: libc_build cat.o
	$(LD) -T linker.ld -o cat $(CRT0) cat.o $(LIBC)

mkdir: libc_build mkdir.o
	$(LD) -T linker.ld -o mkdir $(CRT0) mkdir.o $(LIBC)

touch: libc_build touch.o
	$(LD) -T linker.ld -o touch $(CRT0) touch.o $(LIBC)

rm: libc_build rm.o
	$(LD) -T linker.ld -o rm $(CRT0) rm.o $(LIBC)

rmdir: libc_build rmdir.o
	$(LD) -T linker.ld -o rmdir $(CRT0) rmdir.o $(LIBC)

meminfo: libc_build meminfo.o
	$(LD) -T linker.ld -o meminfo $(CRT0) meminfo.o $(LIBC)

gui_app: libc_build $(GUI_OBJ)
	$(LD) -T linker.ld -o gui_app $(CRT0) $(GUI_OBJ) $(LIBC)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o threads-test fork-test exec-test sh ls cat mkdir touch rm rmdir meminfo gui_app gui/*.o gui/fonts/*.o gui/components/*.o
	find gui/freetype/src -name "*.o" -delete
	$(MAKE) -C $(LIBC_DIR) clean

.PHONY: all clean
