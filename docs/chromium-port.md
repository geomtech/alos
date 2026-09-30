# Port Chromium : etat reel

Chromium, Blink et V8 ne sont **pas encore compiles ni executables sous ALOS**.
Il n'existe pas de faux `/bin/chromium`, de renderer de remplacement ni de
backend Ozone pretendument fonctionnel. Le jalon VM est conserve ; le travail
suivant porte sur le runtime multithreade et la compilation cible de libc++.

## Architecture inspectee

| Surface | Implementation actuelle |
| --- | --- |
| Processus | ELF64 statiques, espaces de pages distincts, `fork`, `execve`, `waitpid`. Fork refuse les processus multithreades. Waitpid renvoie encore le statut ALOS brut. |
| VM | Pages physiques PMM, tables x86-64, heap `brk`, mappings framebuffer et objets SHM. Le nouveau gestionnaire de regions ajoute mmap sparse et les protections. |
| Threads | Preemption Ring 3 effective, FIFO, etat x87/SSE/AVX et FS sauvegarde par thread. Futex prive et pthread de base sont implementes/testes ; pas encore de SMP ou de futex interprocessus. |
| Fichiers | VFS/Ext2, descriptions ouvertes partagees apres fork, fermeture CLOEXEC interne. Stdio non bufferise : fichiers en lecture, sorties console et detection de terminal reelles. Seek et modes fopen en ecriture restent ENOTSUP. |
| IPC | Canaux natifs nommes, attente bloquante, transfert d'objets SHM. Ce protocole n'est pas Mojo ; socketpair/sendmsg/SCM_RIGHTS generiques restent a fournir. |
| GUI | `/bin/gui` est le desktop userland. libgui cree des fenetres, presente des surfaces SHM et recoit des evenements via l'IPC natif. C'est le point d'integration futur d'Ozone, sans X11/Wayland. |
| Reseau | Pile Ethernet/IPv4/TCP/DNS/DHCP et outils natifs. Le dispatcher expose socket/bind/listen/accept/send/recv, pas encore connect/setsockopt/getaddrinfo POSIX complets. |
| Temps/signaux | Horloges monotonic/realtime 64 bits, nanosleep bloquant, echeances de wait queues. Abort/assert terminent tout le processus, y compris les threads CPU-bound ; handlers POSIX de signaux non implementes. |
| Entropie/devices | Pas de getrandom cryptographique ni de devfs `/dev/urandom` identifies. |
| C++ | Clang 18, TLS ELF statique Variant II, init/fini arrays et destructeurs globaux/TLS executes. libc++abi cible compilee ; compilation libc++ cible engagee mais non terminee. |

## Support ajoute

`src/mm/vm.c` gere des regions triees propres a chaque processus, avec
splitting/coalescing, references aux fichiers/objets SHM, et allocation physique
a la faute de page. Les fonctions libc ne sont pas des wrappers malloc.

- `mmap`, `munmap`, `mprotect`, `madvise(MADV_DONTNEED)`.
- `MAP_PRIVATE`, `MAP_SHARED`, `MAP_ANONYMOUS`, `MAP_FIXED`,
  `MAP_FIXED_NOREPLACE`, `MAP_NORESERVE`.
- Reservations anonymes privees sans allocation proportionnelle a leur taille,
  zero-fill au premier acces et liberation sparse des pages/tables.
- `PROT_NONE` conserve les donnees residentes mais interdit l'acces ;
  RW/RX sont implementes par RW/NX des PTE. RWX est refuse avec `EACCES`.
- Mappings prives de fichiers, offsets alignes de 64 bits valides dans les
  limites du VFS 32 bits, zero-fill de la fin de derniere page, et terminaison
  SIGBUS au-dela de la derniere page du fichier.
- Mappings d'objets SHM existants, aliases avec offset et references conserves
  apres close. Les pages partagees restent les memes apres fork, meme si le
  parent ne les a jamais touchees.
- Fork copie les pages privees, y compris les pages residentes PROT_NONE, et
  clone les reservations non residentes. Exec/exit liberent les backings.
- Fault-in dans les copies kernel/user ; read/write refusent les buffers
  invalides au lieu de faire planter le noyau.
- Retour syscall 64 bits signe, six arguments, code executable mmap au-dela
  de 4 GiB pouvant effectuer un syscall.
- `errno` propre a chaque thread natif, via une cellule user privee exposee
  par `SYS_ERRNO_LOCATION`. Ce mecanisme ne remplace pas TLS ELF.
- `sysconf(_SC_PAGESIZE)` et `vminfo` pour verifier les reservations et les
  pages physiques/residentes, sans confondre PMM et heap kernel.

## Limites explicites

L'arena mmap est `[4 GiB, 64 TiB)`, distincte du heap, des surfaces existantes
et de la stack. Les hints hors arena sont consultatifs ; un MAP_FIXED hors
arena ou sur un mapping externe est refuse. Mprotect ne couvre pas encore
les segments ELF et le heap brk.

MAP_SHARED de fichiers ordinaires renvoie ENOTSUP : le VFS n'a pas encore le
page cache coherent/writeback requis. Seuls les objets SHM et les mappings
anonymes partages sont disponibles ; leur backing actuel est eager, limite
a 16 MiB par objet. MADV_DONTNEED partage est refuse, sans faux succes.
Pas de swap ni de COW : les pages privees residentes sont copiees au fork.
Sur x86-64, ecriture/execution peuvent impliquer la lecture ; pas de
protection execute-only materielle ici.

Les wrappers POSIX existants hors fonctions memoire ne sont pas tous
convertis a la convention `-1`/errno. Aucun milestone Chromium A-J ne peut
encore etre annonce.

## Contrats upstream consultes

- [PartitionAlloc, Chromium 140.0.7339.80](https://github.com/chromium/chromium/blob/140.0.7339.80/base/allocator/partition_allocator/src/partition_alloc/page_allocator_internals_posix.h) :
  reservation PROT_NONE, protections, trimming par munmap, remplacement
  MAP_FIXED pour decommit/zero, et MADV_DONTNEED.
- [Plateforme POSIX V8 13.8.258.31](https://github.com/v8/v8/blob/13.8.258.31/src/base/platform/platform-posix.cc) :
  mmap/mprotect, MAP_NORESERVE et sysconf pour la taille des pages.

Ce sont des references de contrat, pas une version Chromium portee.
Il n'y a pas encore de patches GN/Ozone ni de checkout Chromium dans ce depot.
Les futurs scripts/patches restent sous `ports/chromium/`, les sources upstream
et outputs doivent rester externes. Ne pas annoncer `is_alos` utilisable
avant d'avoir une toolchain C++ cible et une target upstream effectivement
compilable.

## Runtime natif implemente et teste

Les tests sont de vrais ELF ALOS compiles avec Clang pour
`x86_64-unknown-none-elf`, pas des executables Linux. C++ utilise le mode
hosted du langage avec `-fno-builtin` et la libc ALOS : cela conserve le nom
ABI `main` sans tirer un CRT ou des headers de l'hote.

- XSAVE/XRSTOR lorsque CPUID le permet, XCR0 x87/SSE/AVX (`0x7`) ; sinon
  FXSAVE/FXRSTOR. Buffers par thread alignes sur 4 KiB, etat initial propre,
  heritage fork et reset exec.
- TLS PT_TLS, tdata/tbss, alignement, TCB et FS base pour chaque thread.
  Relocations Clang `R_X86_64_TPOFF32` verifiees. TLS statique uniquement :
  pas encore de DTV/dlopen ou de modeles TLS dynamiques.
- Futex prive avec comparaison et blocage atomiques sous la gate syscall UP,
  wake compte, EAGAIN, EFAULT et ETIMEDOUT. Les timeouts ont une vraie liste
  retiree au wake/kill, pas une boucle de polling.
- Pthread create/join/detach, mutex normaux/recursifs, condvars/broadcast et
  echeances absolues realtime/monotonic, once, keys et destructeurs.
  Join conserve un resultat kernel independant du thread libere par le reaper.
  Detach reclaim la stack gardee et le descripteur ; TLS/errno sont nettoyes.
- Heap verrouille par futex, alignement malloc 16 octets, allocations alignees
  et controles d'overflow. Init/fini arrays, cxa_atexit/cxa_thread_atexit et
  destructeurs des keys pthread sont reels.
- Errno POSIX est utilise par les nouvelles APIs. Les anciennes wrappers
  ne sont pas toutes homogenisees. Pas encore de pthread cancellation,
  rwlocks, barriers, attributs de creation ou synchronisation process-shared.
- Les sections kernel Ring 0 historiques restent cooperatives/non preemptives.
  Le thread idle reste un fallback : il ne doit pas entrer dans les run queues
  ni recevoir le boost de priorite des taches ordinaires.

## Build et regression

Windows, Docker et QEMU :

```powershell
docker build -t alos-runtime -f ports\chromium\build\Dockerfile.runtime .
.\ports\chromium\scripts\test-vm.ps1
.\ports\chromium\scripts\test-vm.ps1 -Runtime -Cpu qemu64
.\ports\chromium\scripts\test-vm.ps1 -Runtime -SkipBuild -Cpu max
```

Ce script effectue un build groupe kernel/userland, produit un disque de test
separe et demarre QEMU. Il ne reformate pas le `disk.img` utilisateur.
Pour reutiliser les binaires deja construits :

```powershell
.\ports\chromium\scripts\test-vm.ps1 -SkipBuild
```

`/bin/mmap-test` teste reservation de 16 GiB sans frames allouees, fault-in
de deux pages, permissions reelles par enfants fautifs, transitions JIT RW/RX,
syscall depuis code au-dela de 4 GiB, splits, remplacement fixe, discard,
fork prive/partage, offsets, fichier ferme avant fault-in, fin de fichier,
exec, reclamation PMM apres exit, passage de fd SHM via l'IPC existant et
isolation errno entre threads natifs. Les fautes SIGSEGV/SIGBUS de certains
enfants sont attendues, pas des echecs du test. `fork-test`, `exec-test`,
`threads-test`, `ls` et ping du gateway SLIRP completent la regression.

Validation executee dans ALOS/QEMU (1 GiB, un CPU, virtio-net) :
les huit groupes de `/bin/mmap-test` et `ALL PASS`, `fork-test`, les 40
re-executions d'`exec-test`, le shell/VFS et la reponse ICMP sont observes.
Le desktop natif et le lancement de GUI Demo par un vrai clic du launcher
ont aussi ete verifies sur les binaires du meme jalon. Ce n'est pas un test
de rendu Chromium.

Le lot runtime execute trois fois SIMD (400+ switches par execution) et TLS,
deux fois pthread (1000 creations/join par execution, contention et reclamation
PMM/VM verifiee), ainsi que time-test, crt-cxx-test et tls-cxx-test. Les objets
C++ thread_local ont leurs constructeurs/destructeurs verifies sur quatre
threads preemptes. Les assertions restent actives et un enfant avec un thread
CPU-bound doit effectivement etre termine par assert/abort.
Ces tests ont passe avec FXSAVE (`qemu64`) et XSAVE/AVX (`max`).
Le sleep historique de threads-test bloque desormais vraiment ; ce test
de race preexistant reste distinct de la suite pthread de correctness.

Le runner utilise virtio-net, comme `run.ps1`. Un essai PCnet a revele une
taille BAR MMIO preexistante invalide (`0xffffffff00001000`) menant a un
epuisement PMM ; cette anomalie n'est pas corrigee dans ce lot VM.

## Build upstream libc++ reel

Sources LLVM externes, version `llvmorg-18.1.8`,
commit `3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff` :

```powershell
.\ports\chromium\scripts\build-libcxx.ps1 -LLVMDirectory C:\external\llvm-project -OutputDirectory C:\external\alos-libcxx-build
```

Le script verifie la revision, applique le patch horloges ALOS de facon
idempotente, et compile libc++/libc++abi avec un sysroot ALOS isole.
Pas de libc/headers Linux empruntes. Le patch `llvm-18.1.8-alos-clock.patch`
selectionne les vraies clock_gettime ALOS pour chrono, sans annoncer tout
`_POSIX_TIMERS`.

**Compile :** `libc++abi.a` et `libc++.a` pour la cible ALOS, avec LLVM
`llvmorg-18.1.8`. Les deux archives ont ete produites par le build cible isole.
Les erreurs upstream ont guide les corrections de stddef/stdint/limits,
divisions/conversions entieres, allocations alignees, stdio, assertions,
sched_yield, mutex recursifs, error messages et horloges.
`strtof`, `strtod`, `strtold` utilisent le scanner decimal/hexadecimal de musl
adapte au flux memoire ALOS, avec endptr, Inf/NaN et ERANGE. Les auxiliaires
long double x86-64 utilisent egalement les algorithmes musl sous licence MIT.
L'ABI cible est explicitement `LDBL_MANT_DIG=64`, `LDBL_MAX_EXP=16384`,
distincte de `double`. Un comparatif hote de 10 000 chaines decimales aleatoires
entre cette implementation et glibc n'a montre aucun ecart binaire ou `endptr`
pour `strtof` et `strtod`. Les cas limites et malformes sont verifies dans
`/bin/strtod-test`.

Le moteur de formatage printf de musl est adapte aux flux ALOS non bufferises
et aux buffers memoire, sans utiliser la structure FILE de musl. `printf`,
`vprintf`, `fprintf`, `vfprintf`, `sprintf` et `snprintf` partagent ce moteur :
formats entiers, flottants/long double, caracteres UTF-8, largeur/precision
et arguments positionnels. `snprintf` renvoie la longueur totale requise,
meme si la sortie est tronquee, et accepte `(NULL, 0)` pour mesurer celle-ci.
Les sorties longues de `printf` ne sont plus limitees a une stack de 1024
octets ; les erreurs d'ecriture remontent via EOF et l'etat du flux.
`inttypes.h` fournit les macros PRI pour les types exacts, least, fast,
max et pointeurs selon l'ABI LP64 ALOS ; les APIs de scan/conversion
specifiques a inttypes ne sont pas encore fournies.
`/bin/printf-test`, inclus dans le runner `-Runtime`, couvre ces contrats,
les limites des entiers, la troncature, les sorties longues et les erreurs.
Le lot runtime avec ce moteur a passe sous QEMU `qemu64` et `max`.
La construction de libc.a remplace desormais l'archive complete, afin de
ne pas conserver les anciens membres apres suppression d'une source.

**Profil provisoire documente :** exceptions et RTTI desactivees ; filesystem
C++, locale, wide characters, Unicode, timezone database et random_device
exclus. Threads, atomiques, TLS, constructeurs, synchronisation et chrono
restent actifs. Le build upstream est Debug : les assertions ne sont pas
supprimees pour contourner une faute.

L'exclusion de random_device **ne dispense pas l'OS d'entropie cryptographique**.
Getrandom/une source fiable restent obligatoires pour Chromium/BoringSSL,
et ne seront pas remplaces par un PRNG deterministe ou une graine d'horloge.
Ce service n'est simplement pas requis par ce premier bootstrap libc++.

## Blocages suivants

`complex-cpp-test` compile, se lie avec les deux archives C++ et la libc ALOS,
et affiche `ALL PASS` dans QEMU `qemu64`. Il exerce std::string, vector,
unique_ptr, shared_ptr, atomic, quatre std::thread, mutex, recursive_mutex,
condition_variable, chrono, TLS, mmap/mprotect, allocations et destructeurs.
Dix executions consecutives dans QEMU `max`/XSAVE-AVX ont aussi passe. Le suivi
detaille des fuites progressives PMM/VM entre executions reste a faire. Aucun
builtin compiler-rt supplementaire n'a ete requis pour ce lien.

Le premier essai sur le vrai Chromium `base/time/time.cc` a ete lance sur le
tag `140.0.7339.80`, commit `670b6f192f4668d2ac2c06bd77ec3e4eeda7d648`,
dans un checkout externe au depot. Il a atteint le controle de plateforme de
`build/build_config.h` et les buildflags generes manquants. Le patch initial
`ports/chromium/patches/chromium-140.0.7339.80-alos-bootstrap.patch`
introduit `OS_ALOS`, `is_alos` et une toolchain GN x86-64 experimentale.
Le checkout externe a ensuite ete complete et adapte jusqu'a produire le
graphe GN et lancer Ninja sur `base:base`. Ces adaptations supplementaires
du checkout ne sont pas encore toutes consolidees dans le patch bootstrap
du depot : le petit patch initial seul ne reproduit pas ce graphe.
Des objets sont compiles, mais `//base` n'est pas encore construit dans
son ensemble, lie ni execute. Les erreurs observees incluent les headers
`sys/time.h`, `signal.h`, `link.h`, les sockets POSIX et `pthread_rwlock_t`.
Les headers libc++ externes utilises par ce checkout correspondent encore
au profil sans localisation : la regeneration de libc++ avec les nouvelles
options musl/localisation reste a effectuer avant de conclure sur cette API.

Reproduction du test C++ avec les archives upstream :

```powershell
.\ports\chromium\scripts\build-complex.ps1 -LibcxxBuildDirectory <repertoire-build-llvm>
.\ports\chromium\scripts\test-vm.ps1 -SkipBuild -Runtime -ComplexCpp -Cpu qemu64
```

Le patch Chromium s'applique au checkout du tag avec `git apply`. Les sources
Chromium et les produits de build restent externes au depot ALOS.
Le checkout utilise a ete obtenu par :

```powershell
git clone --depth 1 --filter=blob:none --sparse --branch 140.0.7339.80 https://github.com/chromium/chromium.git <repertoire-externe>
git -C <repertoire-externe> sparse-checkout set base build buildtools tools/gn third_party/abseil-cpp third_party/partition_alloc third_party/boringssl third_party/zlib
git -C <repertoire-externe> apply C:\Projets\Alos\ports\chromium\patches\chromium-140.0.7339.80-alos-bootstrap.patch
```

Le `gn gen out/alos` experimental utilise `target_os="alos"`,
`target_cpu="x64"`, `enable_rust=false`, `use_sysroot=false`. Il exige encore
les dependances `DEPS`/gclient, les buildflags generes et les adaptations de
configuration de `//base`. Ce n'est pas une recette de build aboutie.

Ensuite le transport
Mojo devra utiliser des sockets Unix/FD generiques ou un backend natif explicite,
et l'event loop devra attendre plusieurs sources sans polling. Ces besoins
ne sont pas declares satisfaits par les tests d'IPC natif.
