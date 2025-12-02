# Makefile pour les programmes userland ALOS
# Ces programmes sont compilés pour être exécutés par le kernel via le chargeur ELF

CC = i686-elf-gcc
LD = i686-elf-ld

# Flags de compilation
# -ffreestanding : pas de libc standard
# -nostdlib : pas de bibliothèques standard
# -fno-builtin : pas de fonctions builtin
# -m32 : forcer 32 bits
# -I../../userland : pour inclure libc.h
CFLAGS = -ffreestanding -nostdlib -fno-builtin -m32 -O0 -g -Wall -I../../userland

# Adresse de base des programmes utilisateur
# Le kernel charge les programmes à partir de 0x400000
USER_BASE = 0x400000

# Liste des programmes à compiler
PROGRAMS = server.elf

# Cible par défaut
all: $(PROGRAMS)

# Règle générique pour compiler un programme
%.elf: %.o
	$(LD) -Ttext $(USER_BASE) -o $@ $< --entry _start
	@echo "Built $@"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Nettoyer
clean:
	rm -f *.o *.elf

.PHONY: all clean info
