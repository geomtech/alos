
# Userland Makefile
CC = gcc
LD = ld
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -m64 -march=x86-64 -fno-stack-protector -fno-pic -fno-pie -Ilibc/include
LDFLAGS = -static -nostdlib

LIBC_DIR = libc
LIBC = $(LIBC_DIR)/libc.a
CRT0 = $(LIBC_DIR)/crt0.o

GUI_SRC = gui/render.c gui/font.c gui/fonts/roboto.c gui/ssfn_render.c gui/fonts/unifont_sfn.c gui/compositor.c gui/wm.c gui/menubar.c gui/dock.c gui/events.c gui/gui.c
GUI_OBJ = $(GUI_SRC:.c=.o)

all: threads-test gui_app


# Force libc build
.PHONY: libc_build

libc_build:
	$(MAKE) -C $(LIBC_DIR)

threads-test: libc_build threads-test.o
	$(LD) -T linker.ld -o threads-test $(CRT0) threads-test.o $(LIBC)

gui_app: libc_build $(GUI_OBJ)
	$(LD) -T linker.ld -o gui_app $(CRT0) $(GUI_OBJ) $(LIBC)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o threads-test gui_app gui/*.o
	$(MAKE) -C $(LIBC_DIR) clean

.PHONY: all clean
