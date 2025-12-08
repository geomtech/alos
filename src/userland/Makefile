
# Userland Makefile
CC = gcc
LD = ld
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -m64 -march=x86-64 -fno-stack-protector -fno-pic -fno-pie -Ilibc/include
LDFLAGS = -static -nostdlib

LIBC_DIR = libc
LIBC = $(LIBC_DIR)/libc.a
CRT0 = $(LIBC_DIR)/crt0.o

all: threads-test


# Force libc build
.PHONY: libc_build

libc_build:
	$(MAKE) -C $(LIBC_DIR)

threads-test: libc_build threads-test.o
	$(LD) -T linker.ld -o threads-test $(CRT0) threads-test.o $(LIBC)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o threads-test
	$(MAKE) -C $(LIBC_DIR) clean

.PHONY: all clean
