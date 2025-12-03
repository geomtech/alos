# Compatibilité Linux - Linux Binary Compatibility

ALOS inclut désormais une couche de compatibilité pour exécuter des binaires Linux statiquement liés (i386). Cette fonctionnalité permet de lancer des applications Linux compilées pour l'architecture x86 32-bit.

## Fonctionnalités

### Syscalls Linux supportés

La couche de compatibilité traduit les syscalls Linux standard (via `int 0x80`) vers les implémentations natives d'ALOS :

#### Process Management
- `exit` / `exit_group` (1/252) - Terminer un processus
- `getpid` (20) - Obtenir le PID du processus
- `getuid`, `getgid`, `geteuid`, `getegid` (24/47/49/50) - Identifiants utilisateur (retourne 0/root)

#### File Operations
- `read` (3) - Lire depuis un file descriptor
- `write` (4) - Écrire vers un file descriptor
- `open` (5) - Ouvrir un fichier (avec conversion des flags)
- `close` (6) - Fermer un file descriptor
- `chdir` (12) - Changer de répertoire
- `mkdir` (39) - Créer un répertoire
- `getcwd` (183) - Obtenir le répertoire courant
- `access` (33) - Vérifier l'accès à un fichier

#### Memory Management
- `brk` (45) - Changer la limite du heap (stub simplifié)
- `mmap` / `mmap2` (90/192) - Mapper de la mémoire (stub)

#### Network/Sockets
- `socketcall` (102) - Multiplexeur pour opérations socket
  - `socket` (1) - Créer un socket
  - `bind` (2) - Lier un socket
  - `listen` (4) - Mettre en écoute
  - `accept` (5) - Accepter une connexion
  - `send` (9) - Envoyer des données
  - `recv` (10) - Recevoir des données

#### System Information
- `uname` (122) - Obtenir les informations système

### Syscalls non implémentés (stubs)

Ces syscalls retournent `-ENOSYS` (non implémenté) :
- `fork` (2) - Création de processus
- `execve` (11) - Exécution de programmes
- `ioctl` (54) - Contrôle de périphériques
- `fcntl` (55) - Contrôle de file descriptors
- Signaux (signal, sigaction, etc.)

## Utilisation

### Activer le mode Linux

```bash
# Depuis le shell ALOS
linux on

# Vérifier l'état
linux

# Désactiver
linux off
```

### Exécuter un binaire Linux

1. **Compiler votre programme pour Linux i386 (statique)** :

```bash
# Sur votre machine Linux
gcc -m32 -static -o hello hello.c

# Ou avec un cross-compiler
i686-linux-gnu-gcc -static -o hello hello.c
```

2. **Copier le binaire sur le disque ALOS** :

```bash
# Monter le disque ext2
sudo mount -o loop disk.img /mnt

# Copier le binaire
sudo cp hello /mnt/bin/

# Démonter
sudo umount /mnt
```

3. **Exécuter depuis ALOS** :

```bash
# Activer le mode compatibilité
linux on

# Exécuter le programme
exec /bin/hello
```

## Exemple de programme Linux

```c
/* hello.c - Programme Linux simple */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char** argv) {
    printf("Hello from Linux binary!\n");
    printf("Running on ALOS with Linux compatibility\n");
    
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("Current directory: %s\n", cwd);
    }
    
    return 0;
}
```

Compiler avec :
```bash
gcc -m32 -static -o hello hello.c
```

## Limitations actuelles

### Binaires supportés
- ✅ **ELF 32-bit i386 statiquement liés**
- ❌ Binaires dynamiques (nécessite un dynamic linker)
- ❌ Binaires 64-bit
- ❌ Binaires avec dépendances .so

### Fonctionnalités système
- ❌ Fork/exec (pas de création de processus enfant)
- ❌ Signaux POSIX
- ❌ Threads POSIX (pthread)
- ❌ IPC System V (shared memory, semaphores, message queues)
- ❌ Pipes et redirections
- ❌ TTY/terminal control

### Filesystem
- ✅ Opérations basiques (open, read, write, close)
- ✅ Navigation (chdir, getcwd, mkdir)
- ⚠️ Permissions ignorées (pas de système de droits)
- ❌ Liens symboliques
- ❌ Devices (/dev/*)
- ❌ /proc, /sys

### Réseau
- ✅ TCP sockets (socket, bind, listen, accept, send, recv)
- ⚠️ UDP partiel
- ❌ Unix domain sockets
- ❌ Options socket avancées (setsockopt, getsockopt)
- ❌ IPv6

## Architecture technique

### Mapping des syscalls

Le dispatcher de syscalls détecte automatiquement le mode Linux et route les appels :

```
Application Linux (int 0x80)
         ↓
   IDT Entry 0x80
         ↓
  syscall_dispatcher()
         ↓
   [Mode Linux actif?]
         ↓
linux_syscall_handler() → Conversion → Syscalls natifs ALOS
```

### Conversion des flags

Les flags Linux (ex: `O_RDONLY`, `O_WRONLY`) sont automatiquement convertis vers le format interne d'ALOS.

### Gestion des erreurs

Les codes d'erreur ALOS sont convertis en errno Linux (valeurs négatives).

## Tests et débogage

### Logs kernel

Les appels syscalls Linux génèrent des logs visibles avec :
```bash
cat /system/logs/kernel.log
```

### Mode debug

Définir `KLOG_MIN_LEVEL` à `DEBUG` dans `klog.h` pour voir tous les syscalls :

```c
#define KLOG_MIN_LEVEL KLOG_LEVEL_DEBUG
```

### Vérifier le binaire

Avant d'exécuter, vérifier avec :
```bash
elfinfo /bin/hello
```

Doit afficher :
- Class: ELF32
- Machine: i386
- Type: Executable

## Roadmap

### Court terme
- [ ] Améliorer le support de `brk`/`mmap` pour malloc
- [ ] Support basique de `fork`
- [ ] Pipes (pipe, dup, dup2)
- [ ] Stat/fstat complet

### Moyen terme
- [ ] Dynamic linker (ld-linux.so.2)
- [ ] Shared libraries (.so)
- [ ] Signaux basiques (SIGINT, SIGTERM)
- [ ] /proc pseudo-filesystem

### Long terme
- [ ] Threads POSIX (pthread)
- [ ] IPC System V
- [ ] Terminal control (ioctl TIOCGWINSZ, etc.)
- [ ] TTY/PTY complet

## Notes pour développeurs

### Ajouter un syscall Linux

1. Ajouter la définition dans `linux_compat.h` :
```c
#define LINUX_SYS_MYSYSCALL 123
```

2. Implémenter dans `linux_compat.c` :
```c
static int32_t linux_sys_mysyscall(int arg1, int arg2) {
    // Appeler le syscall natif
    extern int sys_native_call(...);
    return sys_native_call(...);
}
```

3. Ajouter au dispatcher :
```c
case LINUX_SYS_MYSYSCALL:
    return linux_sys_mysyscall(arg1, arg2);
```

### Tester la compatibilité

Un bon test est le programme `busybox` statique :
```bash
wget https://busybox.net/downloads/binaries/1.31.0-i686-uclibc/busybox_ASH
chmod +x busybox_ASH
# Copier sur disk.img
```

## Références

- [Linux Syscall Table (i386)](https://github.com/torvalds/linux/blob/master/arch/x86/entry/syscalls/syscall_32.tbl)
- [Linux System Call Interface](https://man7.org/linux/man-pages/man2/syscalls.2.html)
- [ELF Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- [i386 ABI](https://www.uclibc.org/docs/psABI-i386.pdf)

## License

Ce code est fourni à des fins éducatives comme le reste d'ALOS.
