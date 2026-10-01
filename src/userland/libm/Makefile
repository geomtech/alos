CC = clang --target=x86_64-unknown-none-elf
AR = ar
CFLAGS = -std=gnu99 -ffreestanding -fno-builtin -nostdinc -O2 -Wall -Wextra -m64 -march=x86-64 -fno-stack-protector -fno-pic -fno-pie -Iinclude -I../libc/include -Isrc/musl/include

MUSL_GENERIC_EXCLUDED = src/musl/__invtrigl.c src/musl/acosl.c src/musl/asinl.c src/musl/atan2l.c src/musl/atan2f.c src/musl/atanl.c src/musl/ceill.c src/musl/exp2l.c src/musl/expl.c src/musl/expm1l.c src/musl/fabs.c src/musl/fabsf.c src/musl/fabsl.c src/musl/floorl.c src/musl/fma.c src/musl/fmaf.c src/musl/fmodl.c src/musl/llrint.c src/musl/llrintf.c src/musl/llrintl.c src/musl/log10l.c src/musl/log1pl.c src/musl/log2l.c src/musl/logl.c src/musl/lrint.c src/musl/lrintf.c src/musl/lrintl.c src/musl/remainderl.c src/musl/remquol.c src/musl/rintl.c src/musl/sqrt.c src/musl/sqrt_data.c src/musl/math_errors.c src/musl/atan2f.c src/musl/sqrtf.c src/musl/sqrtl.c src/musl/truncl.c
MUSL_ARCH_EXCLUDED = src/musl/x86_64/sqrt.c src/musl/x86_64/sqrtf.c src/musl/x86_64/sqrtl.c
MUSL_GENERIC_SRCS = $(filter-out $(MUSL_GENERIC_EXCLUDED),$(wildcard src/musl/*.c))
MUSL_ARCH_SRCS = $(filter-out $(MUSL_ARCH_EXCLUDED),$(wildcard src/musl/x86_64/*.c))
MUSL_ARCH_ASMS = $(filter-out $(MUSL_ARCH_EXCLUDED:.c=.s),$(wildcard src/musl/x86_64/*.s))
SRCS = src/sqrt.c src/atan2f.c $(MUSL_GENERIC_SRCS) $(MUSL_ARCH_SRCS)
ASMS = $(MUSL_ARCH_ASMS)
OBJS = $(SRCS:.c=.o) $(ASMS:.s=.o)
DEPS = $(SRCS:.c=.d)

all: libm.a

libm.a: $(OBJS) Makefile
	rm -f $@.tmp
	$(AR) rcs $@.tmp $(OBJS)
	mv $@.tmp $@

%.o: %.c Makefile include/math.h src/musl/include/libm.h src/musl/include/features.h
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

%.o: %.s Makefile
	$(CC) -target x86_64-unknown-none-elf -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) libm.a libm.a.tmp

-include $(DEPS)

.PHONY: all clean
