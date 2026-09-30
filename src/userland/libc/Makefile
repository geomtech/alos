
CC = gcc
AR = ar
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra -m64 -march=x86-64 -fno-stack-protector -fno-pic -fno-pie -Iinclude -Isrc
ASFLAGS = -f elf64

SRC_DIRS = src/internal src/stdio src/stdlib src/string src/sys src/unistd src/locale src/musl src/net
SRCS = $(shell find $(SRC_DIRS) -name '*.c')
OBJS = $(SRCS:.c=.o) src/stdlib/setjmp.o

all: libc.a crt0.o

libc.a: $(OBJS)
	rm -f $@.tmp
	$(AR) rcs $@.tmp $^
	mv $@.tmp $@

crt0.o: crt0.s
	nasm $(ASFLAGS) $< -o $@

src/stdlib/setjmp.o: src/stdlib/setjmp.s
	nasm $(ASFLAGS) $< -o $@

# Sources importees de musl (licence MIT, voir COPYRIGHT.musl).
src/musl/%.o: src/musl/%.c
	$(CC) $(CFLAGS) -Isrc/musl/include -include src/musl/include/features.h -Wno-sign-compare -Wno-unused-parameter -Wno-parentheses -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) libc.a libc.a.tmp crt0.o

.PHONY: all clean
