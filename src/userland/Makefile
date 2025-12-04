# Makefile pour les programmes userland ALOS (x86-64)
# Ces programmes sont compilés pour être exécutés par le kernel via le chargeur ELF

CC = x86_64-elf-gcc
LD = x86_64-elf-ld

# Flags de compilation
# -ffreestanding : pas de libc standard
# -nostdlib : pas de bibliothèques standard
# -fno-builtin : pas de fonctions builtin
# -mcmodel=large : pour les adresses 64-bit
# -mno-red-zone : pas de red zone (requis pour le kernel)
# -I../../src/lib : pour inclure libc.h
CFLAGS = -ffreestanding -nostdlib -fno-builtin -mcmodel=large -mno-red-zone -O0 -g -Wall -I../../src/lib

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
