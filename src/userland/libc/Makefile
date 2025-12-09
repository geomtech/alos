
CC = gcc
AR = ar
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -m64 -march=x86-64 -fno-stack-protector -fno-pic -fno-pie -Iinclude -Isrc
ASFLAGS = -f elf64

SRC_DIRS = src/internal src/stdio src/stdlib src/string src/sys src/unistd
SRCS = $(shell find $(SRC_DIRS) -name '*.c')
OBJS = $(SRCS:.c=.o) src/stdlib/setjmp.o

all: libc.a crt0.o

libc.a: $(OBJS)
	$(AR) rcs $@ $^

crt0.o: crt0.s
	nasm $(ASFLAGS) $< -o $@

src/stdlib/setjmp.o: src/stdlib/setjmp.s
	nasm $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libc.a crt0.o

.PHONY: all clean
