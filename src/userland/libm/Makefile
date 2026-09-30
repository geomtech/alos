CC = clang --target=x86_64-unknown-none-elf
AR = ar
CFLAGS = -std=gnu99 -ffreestanding -fno-builtin -nostdinc -O2 -Wall -Wextra -m64 -march=x86-64 -fno-stack-protector -fno-pic -fno-pie -Iinclude -I../libc/include -Isrc/musl/include
SRCS = $(wildcard src/*.c src/musl/*.c)
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

all: libm.a

libm.a: $(OBJS) Makefile
	rm -f $@.tmp
	$(AR) rcs $@.tmp $(OBJS)
	mv $@.tmp $@

%.o: %.c Makefile include/math.h src/musl/include/libm.h src/musl/include/features.h
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) libm.a libm.a.tmp

-include $(DEPS)

.PHONY: all clean
