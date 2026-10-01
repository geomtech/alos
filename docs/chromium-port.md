# Port Chromium : etat reel

La cible Chromium complete, Blink et V8 ne sont **pas encore construits,
lies ni executes sous ALOS**. GN produit un graphe experimental et Ninja
compile de vrais objets de `//base` et de ses dependances.
Il n'existe pas de faux `/bin/chromium`, de renderer de remplacement ni de
backend Ozone pretendument fonctionnel. Le jalon VM est conserve ; le travail
suivant porte sur les services runtime manquants et l'integration upstream.

## Architecture inspectee

| Surface | Implementation actuelle |
| --- | --- |
| Processus | ELF64 statiques, espaces de pages distincts, `fork`, `execve`, `waitpid`. Fork refuse les processus multithreades. Waitpid renvoie encore le statut ALOS brut. |
| VM | Pages physiques PMM, tables x86-64, heap `brk`, mappings framebuffer et objets SHM. Le nouveau gestionnaire de regions ajoute mmap sparse et les protections. |
| Threads | Preemption Ring 3 effective, FIFO, etat x87/SSE/AVX et FS sauvegarde par thread. Futex prive et pthread de base sont implementes/testes ; pas encore de SMP ou de futex interprocessus. |
| Fichiers | VFS/Ext2, offsets partages fork/dup, CLOEXEC, lseek/pread/pwrite/ftruncate, fcntl partiel, repertoires et stat. Stdio non bufferise : fopen/fdopen r/w/a/+ et fseek/ftell reels. Offset VFS borne a 32 bits ; advisory locks et positionnement relatif avec pushback wide restent limites. |
| IPC | Canaux natifs nommes, attente bloquante, transfert d'objets SHM. Ce protocole n'est pas Mojo ; socketpair/sendmsg/SCM_RIGHTS generiques restent a fournir. |
| GUI | `/bin/gui` est le desktop userland. libgui cree des fenetres, presente des surfaces SHM et recoit des evenements via l'IPC natif. C'est le point d'integration futur d'Ozone, sans X11/Wayland. |
| Reseau | Pile Ethernet/IPv4/TCP/DNS/DHCP et outils natifs. socket/bind/listen/accept/connect/send/recv, poll, un sous-ensemble epoll, AF_UNIX socketpair/SCM_RIGHTS et getaddrinfo IPv4/DNS sont disponibles ; setsockopt, IPv6 et les sockets Unix nommes restent incomplets. |
| Temps/signaux | Horloges monotonic/realtime 64 bits, nanosleep bloquant, echeances de wait queues. Abort/assert terminent tout le processus, y compris les threads CPU-bound ; handlers POSIX de signaux non implementes. |
| Entropie/devices | getentropy via legacy VirtIO RNG host-backed, avec prerequis de confiance du deploiement et echec ferme sans device. Pas de getrandom ni de devfs `/dev/urandom` ; aucun test de sante cryptographique/profil production etabli. |
| C++ | Clang 18, TLS ELF statique Variant II, init/fini arrays et destructeurs globaux/TLS executes. Archives libc++/libc++abi cibles compilees avec localisation ; complex-cpp-test recompile/lie avec ce profil et execute trois fois avec succes sous QEMU max, y compris locale classique et streams narrow. |

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
Un patch bootstrap GN/`OS_ALOS`/`is_alos` existe sous `ports/chromium/`,
mais pas de backend Ozone fonctionnel. Le checkout Chromium et ses outputs
restent externes. Le graphe GN et des objets compiles ne prouvent pas une
target upstream complete utilisable.

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
  barriers ou synchronisation process-shared.
- Les attributs de creation pthread supportent init/destroy, taille de pile
  et creation detachee ; `pthread_getattr_np` interroge les piles natives.
  Pas de piles externes fournies par l'appelant ni d'API complete d'attributs
  ou scheduling. `gettid` expose l'identifiant numerique natif, distinct du
  handle opaque `pthread_t`.
- `pthread_rwlock_*` permet plusieurs lecteurs simultanes et un ecrivain
  exclusif, avec blocage par mutex/condvar/futex. Preference lecteurs, sans
  garantie FIFO ; attributs, timed locks et process-shared non implementes.
  Les acquisitions try utilisent aussi trylock sur la gate interne, sans
  attente bloquante lorsque celle-ci est occupee.
- `CLOCK_BOOTTIME=2`, `CLOCK_MONOTONIC_RAW=3` et `CLOCK_MONOTONIC_COARSE=4`
  partagent l'uptime kernel de MONOTONIC, a resolution milliseconde :
  aucun suspend/resume ni ajustement NTP n'est implemente.
  `gettimeofday` utilise REALTIME et remplit une `struct timezone` fournie
  avec `tz_minuteswest=0` et `tz_dsttime=0` (UTC).
  Les routines musl `gmtime`/`gmtime_r`,
  `timegm`, `strftime`/`strftime_l` fournissent le calendrier UTC, sans
  regles de timezone locale. `time`, `localtime`/`localtime_r` et `mktime`
  sont implementes avec UTC comme seul profil local, y compris normalisation
  des dates par `mktime`.
  `strptime` parse la locale C ; `tzset` conserve UTC et signale ENOTSUP
  pour un nom TZ non supporte, sans charger de regles locales/DST.
- `CLOCK_THREAD_CPUTIME_ID=5` utilise la comptabilisation PIT du thread
  courant, pas un alias uptime. Les switches n'ajoutent plus une seconde
  fois la duree deja comptee par les ticks.
- `CLOCK_REALTIME_COARSE=6` partage la vraie base REALTIME, a resolution
  milliseconde. `usleep` convertit les microsecondes en secondes/nanosecondes
  et utilise `nanosleep` bloquant, sans attente active.
- `bsearch` effectue une vraie recherche binaire ; les bornes entieres et
  flottantes des headers libc suivent les macros ABI du compilateur.
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

Depuis le checkpoint `535499857c3e7a33f580b5d0e7be617c9f043fad`,
les logs natifs du 2026-09-30 sous le dossier de session parent
`C:\Users\dacru\.copilot\session-state\cd381fa3-e064-48af-b572-fb8597852ac3\files`
montrent `pthread-rwlock-test: PASS` dans `rwlock-vm\serial.log` (qemu64)
et `rwlock-vm-max\serial.log` (max). Le test verifie quatre lecteurs
simultanes, puis quatre lecteurs et deux ecrivains avec 3000 acquisitions
par thread, l'exclusion, EBUSY/EINVAL, destruction/reinitialisation et errno.
`locale-vm\serial.log` et `scanf-vm\serial.log` montrent `printf-test: PASS`
sur qemu64 pour les ajouts asprintf/conversions `_l` puis sscanf/vsscanf.
`calendar-vm\serial.log` montre `time-test: PASS` et `printf-test: PASS`
sur qemu64 pour les nouveaux cas calendrier UTC/horloges et pushback de flux.
Ces preuves portent sur les sources locales ajoutees depuis ce checkpoint.
Le jalon C++ avec localisation valide ensuite est distingue ci-dessous.
Le lot ulterieur `test-vm.ps1 -Runtime -Cpu qemu64` est termine dans
`base-time-vm\serial.log` (`vm-suite-complete`), avec `time-test: PASS`
apres ajout des champs timezone UTC et de MONOTONIC_COARSE, et
`pthread-rwlock-test: PASS` apres ajout du cas gate occupee pour les
acquisitions try. Cela ne constitue pas un resultat de build `//base`.
Les lots suivants `thread-clock-vm\serial.log` (qemu64) et
`scalar-vm\serial.log` (runtime, max) montrent aussi `time-test: PASS`
et `vm-suite-complete`. Les nouveaux cas verifient CPU monotone borne
par le temps ecoule (tolerance de deux millisecondes), puis au plus 10 ms
CPU pendant un sommeil de 100 ms ; calendrier epoch -1, annee bissextile
2000 et normalisation du 30 fevrier en 1er mars. Le lot scalar contient
egalement `printf-test: PASS` apres les ajouts bsearch et bornes ABI.
Apres integration des corrections paralleles et rebuild kernel/userland,
`parallel-fixes-vm\serial.log` (qemu64) contient `pthread-attr-test: PASS`,
`thread-id-test: PASS`, `calendar-test: PASS`, `libc-common-test: PASS`,
les marqueurs runtime/wide existants et `vm-suite-complete` ; `build.log`
termine la creation de l'ISO. Les tests couvrent piles et reclamation
join/detach, identifiants natifs stables/uniques, parsing C/UTC, `expf`
derive de musl (21 references binary32 et balayage monotone), `atol`,
`fputs` et `perror`. Ces sorties console sont reelles ; les tentatives
d'ecriture fichier remontaient EIO/erreur de flux et la fixture restait intacte
a ce jalon, avant le support FD read/write ajoute ci-dessous.

Le runner utilise virtio-net, comme `run.ps1`. Un essai PCnet a revele une
taille BAR MMIO preexistante invalide (`0xffffffff00001000`) menant a un
epuisement PMM ; cette anomalie n'est pas corrigee dans ce lot VM.

## Build upstream libc++ reel

Sources LLVM externes, version `llvmorg-18.1.8`,
commit `3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff` :

```powershell
.\ports\chromium\scripts\build-libcxx.ps1 -LLVMDirectory C:\external\llvm-project -OutputDirectory C:\external\alos-libcxx-build
```

Le script verifie la revision, applique les patches horloges ALOS et
`llvm-18.1.8-musl-narrow-locale.patch` de facon
idempotente, et compile libc++/libc++abi avec un sysroot ALOS isole.
Pas de libc/headers Linux empruntes. Le patch `llvm-18.1.8-alos-clock.patch`
selectionne les vraies clock_gettime ALOS pour chrono, sans annoncer tout
`_POSIX_TIMERS`.
Le patch musl narrow locale protege les inclusions/utilisations wide
inconditionnelles du support xlocale lorsque les caracteres C++ wide sont
desactives.

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

`sscanf`/`vsscanf` ont ete ajoutes : conversions entieres et flottantes/long
double, suppression, largeur, scansets, wide et `%n` sont couverts dans
`printf-test`, sans annoncer une completude POSIX. `getc`/`putc` et `ungetc`
existent ; le pushback d'un octet est consomme par `fread`. `asprintf` et
les conversions flottantes `_l` sont egalement testes. Les locales restent
limitees aux noms C/POSIX/UTF-8, aux formats C et a la collation par octets
ou points de code, sans base de donnees de locales.

**Profil courant compile :** localisation, Unicode et caracteres wide C++
actives ; exceptions et RTTI desactivees ; filesystem C++, timezone database
et random_device exclus.
Threads, atomiques, TLS, constructeurs, synchronisation et chrono
restent actifs. Le build upstream est Debug : les assertions ne sont pas
supprimees pour contourner une faute.

Le build cible `alos-libcxx-loc-build`, sous
`C:\Users\dacru\.copilot\session-state\31de8139-773c-483f-92cd-f69ef353a4ae\files\runtime-llvm`,
a termine : `configure.log` confirme la generation et `build.log` la liaison
statique de `lib/libc++.a` ; les deux archives existent. `CMakeCache.txt`
confirme LOCALIZATION/UNICODE ON et WIDE_CHARACTERS OFF pour ce premier
jalon de localisation narrow ; `__config_site`
confirme musl/pthread et les exclusions. `complex-cpp-test` a ensuite ete
recompile/lie avec ce profil et execute avec succes, comme detaille ci-dessous.

L'exclusion de random_device **ne dispense pas l'OS d'entropie cryptographique**.
Une source fiable reste obligatoire pour Chromium/BoringSSL,
et ne seront pas remplaces par un PRNG deterministe ou une graine d'horloge.
Ce service n'est simplement pas requis par ce premier bootstrap libc++.
Le jalon fleet suivant ajoute getentropy host-backed sous conditions de
deploiement, sans fournir getrandom ni prouver un runtime crypto Chromium.

## Blocages suivants

Avec l'ancien profil **sans localisation**, `complex-cpp-test` compile,
se lie avec les deux archives C++ et la libc ALOS,
et affiche `ALL PASS` dans QEMU `qemu64`. Il exerce std::string, vector,
unique_ptr, shared_ptr, atomic, quatre std::thread, mutex, recursive_mutex,
condition_variable, chrono, TLS, mmap/mprotect, allocations et destructeurs.
Dix executions consecutives dans QEMU `max`/XSAVE-AVX ont aussi passe. Le suivi
detaille des fuites progressives PMM/VM entre executions reste a faire. Aucun
builtin compiler-rt supplementaire n'a ete requis pour ce lien.

Avec le **nouveau profil avec localisation**, `build-complex.ps1` utilisant
`runtime-llvm\alos-libcxx-loc-build` a recompile et lie `complex-cpp-test`.
Le programme teste aussi `std::locale::classic()`, les facets
`std::numpunct<char>`/`std::ctype<char>`, `ostringstream` avec precision fixe
et `istringstream` pour doubles/entiers. La commande
`test-vm.ps1 -Runtime -SkipBuild -ComplexCpp -ComplexRuns 3 -Cpu max`
a execute trois fois ce binaire natif : `locale-complex-vm\serial.log`,
dans le dossier parent indique plus haut, contient trois marqueurs
`classic locale streams PASS` et trois `ALL PASS`. Ce jalon ne valide
ni les caracteres wide C++, ni une cible Chromium complete.
Apres correction de la comptabilisation CPU, un nouveau build/lien et
trois executions qemu64 ont aussi passe : `scalar-complex-vm\serial.log`
contient `time-test: PASS`, trois `classic locale streams PASS`, trois
`ALL PASS` et `vm-suite-complete`.

Le jalon suivant active **WIDE_CHARACTERS ON** dans `build-libcxx.ps1`,
avec LOCALIZATION/UNICODE ON et la meme revision LLVM 18.1.8. Les archives
cibles ont ete recompilees, puis `complex-cpp-test` recompile/lie. Preuves
sous `C:\Users\dacru\.copilot\session-state\91acf6a3-8e4f-4dc3-88cf-873d1747f865\files` :
`wide-libcxx-configure.log`, `wide-libcxx-build.log` et
`wide-complex-build.log`. `wide-qemu64-validated\serial.log` et
`wide-max-validated\serial.log` montrent chacun `printf-test: PASS`,
`wide-format-test: PASS`, trois `complex-cpp-test: ALL PASS` avec destructeur
global et `vm-suite-complete`. Aucun build/lien/execution Chromium n'en decoule.

`swprintf`/`vswprintf` utilisent un parseur derive de musl (MIT) avec un puits
`wchar_t` direct pour Unicode, largeur/precision et `%n` ; seules les
conversions numeriques ASCII passent par `snprintf` type. Les tests couvrent
long double x87, positionnels/largeurs `*`, longueurs en caracteres wide,
troncature negative avec NUL et erreurs de format/overflow.
`getwc`/`fgetwc`/`fputwc`/`ungetwc` font de vraies conversions UTF-8, avec
un caractere wide de pushback ; `fread` et `ungetc` refusent un pushback wide
en attente avec EINVAL. Pas d'orientation generale ni de `fwide`.
L'ecriture console wide est verifiee en octets bruts
`C3 A9 F0 9F 98 80 00`. A ce premier jalon wide, l'ecriture fichier etait
encore non supportee :
`fputwc` renvoyait WEOF/EIO et positionnait l'erreur du flux, sans modifier la
fixture. Les fixtures Unicode/EOF/invalide/incomplete/pushback utilisent
le disque de test isole. Le jalon FD suivant remplace cette limitation.

Le lot `fd-metadata-vm-fixed\serial.log` du dossier parent, runtime qemu64,
valide ensuite read/write sur fichiers existants, extension Ext2 et trous
zero-fill, `lseek`, `dup`/`dup2` et `fcntl` partiel. Les offsets/flags OFD
sont partages, CLOEXEC est propre au descripteur ; les tests couvrent fork,
exec, append, erreurs d'acces et pointeurs invalides. `opendir`/`fdopendir`,
`readdir`/`closedir`/`dirfd` et `stat`/`lstat`/`fstat` exposent de vraies
entrees/metadonnees inode, avec erreurs explicites sur repertoires malformes.
L'ecriture FILE wide, `fputs` et `perror` est maintenant relue sur fichier ;
les descripteurs read-only renvoient EBADF, pas un faux succes.
Restent non implementes a ce jalon FD initial : creation/truncation/sync
(ajoutes au jalon fleet ci-dessous), path-at, application complete des
permissions et suivi de liens symboliques ; `fopen` en
ecriture et `fseek` restaient ENOTSUP a ce jalon, malgre le vrai `lseek` natif.
Le log contient `fd-io-test: PASS`, `fs-metadata-test: PASS`, les tests
runtime/wide et `vm-suite-complete`. Le premier lot `fd-metadata-vm`
avait echoue sur le cas atan2f infini ; la constante pi/2 a ete corrigee
avant ce lot valide, sans relacher le test.
Le lot concurrent `fd-metadata-complex-vm` a expire pendant TLS C++ et
n'est pas declare reussi. La reprise isolee `fd-metadata-max-isolated\serial.log`
valide ensuite le runtime max, FD/metadonnees, trois `complex-cpp-test: ALL PASS`
et `vm-suite-complete`. `coarse-usleep-vm\serial.log` valide le runtime qemu64
avec `time-test: PASS` et `vm-suite-complete` : le test controle REALTIME_COARSE,
un sommeil de 20001 us prenant au moins 21 ms et `usleep(0)`.

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
son ensemble, lie ni execute. Les anciens logs, dont `out\ninja-rwlock.log`,
utilisaient les headers du profil `/libcxx` sans localisation ; leurs erreurs
sur les headers POSIX, sockets et rwlocks ne decrivent pas toutes l'etat
actuel de la libc. La regeneration avec localisation a maintenant reussi.
La tentative Ninja avec ce nouveau profil s'est terminee avec 185 actions
`FAILED` et `ninja: build stopped: cannot make progress due to previous errors.`
Preuve : `C:\Users\dacru\.copilot\session-state\3da64af5-a6a6-4756-a013-a8b1656d6590\files\ninja-localization.log`.
Les erreurs incluent `std::wstring`/`wstring_view` et les headers wide C++
exclus par le profil, `CLOCK_THREAD_CPUTIME_ID`, `signal.h` et `link.h`.
Ce diagnostic est celui de cette tentative du 2026-09-30, pas un inventaire
definitif des sources courantes : les corrections timezone UTC et
MONOTONIC_COARSE ont ete validees nativement apres ce run, sans nouvelle
validation Ninja etablie ici. Le code retour du shell de capture ne prouve
pas le succes Ninja ; la fin du log indique explicitement son echec.
La localisation narrow ne suffit donc pas a construire `//base` ; aucun
resultat de cible complete, de lien ou d'execution n'est etabli. Le checkout
verifie est `C:\Users\dacru\Documents\Codex\2026-09-30\c\work\chromium-140.0.7339.80`,
au commit Chromium indique ci-dessus.

Le patch persistant `chromium-140.0.7339.80-alos-clocks.patch`, applique par
`build-chromium.ps1`, ouvre explicitement la gate `ClockNow` avec `__ALOS__`
dans les deux `time_now_posix.cc` upstream, sans annoncer toute l'option
POSIX. L'objet PartitionAlloc `time_now_posix.o` compile dans Ninja ;
`obj/base/base/time_now_posix.o` a compile isole par sa commande extraite
avec `ninja -t commands`. Cela ne valide pas sa cible dans le graphe :
des dependances Perfetto manquent encore, dont sys/utsname et signaux.
Le nouveau run global `out\ninja-cpu-clock.log` s'est termine en echec
(`cannot make progress due to previous errors`). Les erreurs wide C++
(`wstring`/`wstring_view`), `signal.h` et `link.h` restent visibles.
La recherche des diagnostics `error:` de ce log ne retrouve plus
`CLOCK_THREAD_CPUTIME_ID`, `bsearch` ou `DBL_` ; cette absence de diagnostics
ne valide pas les cibles completes. Aucun lien/execution Chromium n'est etabli.
Ce run precede le profil WIDE ON valide ci-dessus : ses erreurs wide ne
sont pas une preuve d'echec du nouveau profil dans Chromium. Aucune nouvelle
validation de ces anciens diagnostics n'en decoule.
La tentative suivante avec WIDE ON, `out\ninja-wide.log`, s'est elle aussi
arretee avec `cannot make progress due to previous errors` et 85 actions
`FAILED`. Elle precede l'integration des corrections paralleles ci-dessus ;
pas de cible `//base` complete, lien executable ou execution Chromium.

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

Le profil bootstrap explicite du builder garde `use_partition_alloc=true`
mais fixe `use_allocator_shim=false`, `use_partition_alloc_as_malloc=false`,
`enable_backup_ref_ptr_support=false` et `enable_pointer_compression_support=false`.
PartitionAlloc reste un allocateur explicite a compiler ; malloc reste celui
d'ALOS, sans interposition. Les assertions/invariants ne sont pas desactives
pour accepter des options contradictoires. Ce profil ne fournit pas les
protections allocator du navigateur. Le Ninja `out\ninja-no-shim.log`
s'est termine avec 20 actions `FAILED` et `cannot make progress due to
previous errors`. Ses diagnostics ne mentionnent plus usleep,
CLOCK_REALTIME_COARSE ou le shim malloc ; les sockets/BIO, getentropy
et les services IO/systeme Perfetto restent des blocages.
Cela ne valide aucune cible `//base` complete, lien ou execution.

Le premier lot natif fleet `fleet-vm\serial.log`, sous le dossier parent
indique plus haut, montre `env-test: exec inherited constructor PASS`,
`env-test: exec empty PASS`, `env-test: PASS`, `uname-test: PASS` et
`posix-file-test: PASSED`. Ce sont des resultats partiels : la suite complete
n'est pas terminee/validee et aucun nouveau Ninja n'est etabli depuis
la baseline a 20 actions en echec. Aucun service d'entropie cryptographique
valide n'est deduit de ce lot ; les corrections et tests suivants restent
a confirmer avant de remplacer les limitations courantes a ce premier essai.

La validation collective suivante termine les deux lots qemu64
`fleet-no-rng-vm\serial.log` et `fleet-rng-vm\serial.log` avec
`vm-suite-complete`, env/constructeurs exec herite ou vide, uname,
POSIX-file, pipe et socket PASS. Le premier confirme
`entropy-test: unavailable PASS` (ENOSYS, buffers preserves) ; le second
confirme l'initialisation legacy RNG puis `entropy-test: source/concurrent PASS`,
explicitement sans test de sante cryptographique.
Les ajouts valides incluent O_CREAT/O_EXCL/O_TRUNC/O_SYNC, creation 0600/0700,
fsync/flush ATA, pipes bloquants et nettoyage apres terminaison explicite
SYS_EXIT_GROUP (_exit reste thread-only), environnement fork/exec/threads,
uname et sockets IPv4 passives/poll. EPIPE n'emet pas SIGPIPE ; access ne
supporte que F_OK et refuse les autres modes avec ENOTSUP. Les temporaires
utilisent PID/compteur et creation exclusive, pas des noms cryptorandom.
Connect reste EOPNOTSUPP, getaddrinfo numeric-only ; AF_UNIX/SCM_RIGHTS,
IPv6 et options sockets completes restent a fournir, avec limitations
TCP passif restantes. Les headers compilables ne prouvent pas ces capacites.
Le RNG exige le backend hote de confiance decrit dans `docs/entropy.md` :
la chaine Windows QEMU/GnuTLS/BCrypt observee n'authentifie pas les binaires.
Le driver bloque, garde quatre pages DMA epinglees et impose un budget
de dix secondes avec echec ferme ; aucune garantie production/Chromium
cryptographique n'est annoncee. Le nouveau Ninja `out\ninja-fleet.log`
s'est termine avec deux actions `FAILED`, contre 20 a la baseline :
BoringSSL `bio/file.cc` demandait `fgets` et Perfetto `android_utils.cc`
`_SC_NPROCESSORS_CONF`. La fin de `out\ninja-fleet.log` indique
`cannot make progress due to previous errors` ; pas de cible complete,
lien ou execution Chromium.

`fgets` a ensuite ete implemente via `fgetc` et son pushback, avec newline
conservee, terminaison NUL (taille un sans consommer), EOF partiel et erreurs
de flux. `_SC_NPROCESSORS_CONF` renvoie un pour le kernel configure UP,
pas un compte CPUID. Les cas sont dans `uname-test` ; le lot
`fleet-fgets-vm\serial.log` montre uname/entropie PASS et `vm-suite-complete`.
Le lot `fleet-max-complex-verified\serial.log` valide egalement le runtime
fleet max et trois `complex-cpp-test: ALL PASS`, avec entropie et fin de suite.
Le premier runner max avait classe a tort `(0 failures)` comme echec ;
ce classement a ete corrige sans supprimer les assertions, puis la suite
relancee. Le Ninja suivant `out\ninja-fgets.log` s'est termine avec
217 actions `FAILED` et `cannot make progress due to previous errors`.
Les deux echecs precedents bloquaient des dependances : leur correction
a ouvert des objets `//base`/Perfetto plus profonds, sans prouver une
regression native. Le log contient 156 diagnostics de header
`gtest_prod.h` manquant dans le checkout upstream et 36 de `signal.h`
manquant. Ces comptes ne sont ni 217 APIs absentes ni un inventaire complet
des causes. Aucune cible `//base` complete ni smoke test compile/lie/execute
n'est etabli ; le prototype `base-smoke.cc` n'est pas une preuve d'execution.

Apres restauration des dependances googletest, `out\ninja-gtest-deps.log`
termine encore en echec avec 118 actions `FAILED`. La tentative suivante
`out\ninja-native-backends.log` termine en echec avec 52 actions `FAILED`
et `cannot make progress due to previous errors`, dont des cascades
`sys/epoll.h` manquant et d'autres besoins production. Ce compte mesure
des actions, pas 52 APIs ou causes distinctes. Les targets GN de tests
natifs et leurs controles de compilation ne prouvent pas un executable
lie ou execute, ni une cible `//base` complete.
Le lot natif ALOS `positional-stdio-vm\serial.log` montre en revanche
`stdio-file-test: PASS`, `positional-io-test: PASSED`, wide PASS et
`vm-suite-complete`. Il valide fopen r/w/a/update/binary, fdopen sans
troncature avec ownership et modes d'acces, fseek/ftell avec EOF/pushback
octet, pread/pwrite sans mouvement du curseur partage (y compris O_APPEND),
et ftruncate/ftruncate64 avec extension sparse zero-fill et reclamation
des blocs directs/indirects a la reduction. La borne VFS 32 bits remonte
EOVERFLOW ; F_SETLK/F_GETLK/F_SETLKW restent ENOTSUP. Avec pushback wide
en attente, ftell et fseek SEEK_CUR refusent explicitement ENOTSUP ;
aucune orientation generale de flux n'est ajoutee.

Le lot `mincore-memory-vm\serial.log` du dossier parent termine le runtime
fleet qemu64 avec `mincore-test: PASS`, `system-info-test: PASS` et
`vm-suite-complete`. mincore interroge la residence reelle sans provoquer
de faute sur les donnees consultees : sparse/PROT_NONE, ELF/stack/heap
et vecteur de 257 pages sont testes avec compteurs inchanges.
L'API system-memory rapporte la capacite RAM utilisable au boot geree
par PMM, les octets libres allouables et zero swap, pas l'adresse physique
maximale, une capacite DIMM ou un RSS complet. Ce jalon natif ne change
pas la derniere baseline Ninja verifiee de 52 actions en echec ; aucune
cible `//base` complete ni executable Chromium lie/execute n'est etabli.

Le lot suivant a ete integre et controle directement apres interruption des
agents. `frontier-rights-vm\serial.log` passe le runtime fleet sur qemu64 :
readv/writev et FIONREAD, allocations SHM natives, descripteurs read-only et
plafonds mprotect conserves apres split/fork/dup, limites NOFILE/DATA et
nice du thread courant, ldexpf et statistiques du heap libc.
Les chemins legacy shm_map et IPC refusent un descripteur SHM read-only
plutot que de lui rendre des droits d'ecriture. Le transport SCM_RIGHTS
generique n'est pas fourni.

`frontier-rights-max-vm\serial.log` passe aussi le runtime fleet sur max
avec trois executions de complex-cpp-test apres reconstruction de libc++.
`frontier-rights-no-rng-vm\serial.log` verifie l'echec ENOSYS sans device
RNG, sans repli faible. Les getters rlimit exposent NOFILE fixe a 32 et
DATA sans quota configure ; les setters et priorites POSIX generiques
restent ENOTSUP. L'API privee de nice agit reellement sur le thread courant,
sans annoncer de permissions uid/RLIMIT_NICE ni de priority inheritance.
dlopen/dlsym/dladdr echouent explicitement avec ENOTSUP et dlerror :
aucun chargeur dynamique ou renseignement de module statique n'est simule.
Ces resultats natifs ne valident pas encore les backends C++ Base/Perfetto
ni une cible complete ; `out\ninja-frontier-audited.log` est la relance
destinee a mesurer cette nouvelle frontiere.

Cette relance exacte `ninja -C out/alos -k 0 -j 8 base:base` termine avec
13 actions `FAILED` et 22 diagnostics de compilation. Le dernier compteur
affiche est `2037/2038` : il inclut des tentatives en echec et ne mesure
pas un pourcentage de compilation reussie. La baseline preservee de
52 actions reste disponible dans les artefacts de session.
Les causes restantes observees comprennent les signaux de processus,
les credentials Unix, statvfs/groupes, futimes, msync/MADV_FREE et
plusieurs choix de backend Base. L'ID de thread et les metriques malloc
ont ensuite ete corriges vers les APIs natives reelles ; leurs commandes
de compilation isolees passent, mais une nouvelle validation globale
reste necessaire. Aucune cible Base complete, archive finale ni executable
Base lie/execute n'est annonce.

La verification suivante `out\ninja-native-metrics.log` termine avec
9 actions `FAILED`, contre 13 auparavant. Les quatre actions retirees
utilisent maintenant l'ID thread numerique natif, les statistiques du
heap libc dans les reporters malloc, et un refus explicite des credentials
Unix indisponibles ; aucun ucred, SCM_CREDENTIALS ou PID authentifie
n'est invente. Les blocages observes restants sont les trois sources de
processus/signaux, statvfs, groupes, setproctitle, futimes et les operations
msync/MADV_FREE. Ils ne sont pas declares implementes par leurs seules
constantes ou declarations.

La reprise suivante a reproduit ces neuf echecs avant modification.
`fs-attributes-vm` et `msync-fs-vm` passent ensuite le runtime fleet qemu64 ;
`fs-msync-max-vm` passe le meme lot sous max avec trois complex-cpp-test.
statvfs expose les vrais blocs/inodes libres Ext2, l'identifiant de montage
ALOS et la granularite du volume. Les blocs reserves par uid ne sont pas
enforces ; tous les blocs libres restent allouables dans ce profil.
futimes persiste atime/mtime et met ctime a l'heure courante, a la resolution
Ext2 d'une seconde ; les fractions sont tronquees et les dates hors du
format unsigned 32 bits sont refusees avec EOVERFLOW.
Le nom de processus natif est modifiable jusqu'a 31 octets et visible dans
ps ; les titres trop longs echouent, sans modifier argv ni feindre les
semantiques BSD de setproctitle. Base appelle cette API explicitement.
grp.h n'est plus inclus sur ALOS : ses seuls consommateurs dans ce fichier
restent dans le bloc Mac, sans base de groupes native inventee.

msync valide les mappings mmap et SHM legacy et assure la barriere de
coherence pour leurs backings en RAM, sans provoquer de faute sur les pages
consultees. Les changements MAP_PRIVATE ne sont pas ecrits dans le fichier.
L'invalidation d'une copie privee de fichier retourne ENOTSUP ; les fichiers
MAP_SHARED, leur page-cache/writeback et la reclamation MADV_FREE ne sont
toujours pas implementes. `msync-test` verifie ces limites, les trous,
protections, flags, donnees preservees et absence d'allocation au sync sparse.
Les mappings externes non identifies comme SHM (y compris ELF/MMIO/framebuffer)
sont refuses avec ENOTSUP : une PTE user presente ne suffit pas a prouver
qu'un backing est de la RAM synchronisable.
La relance globale correspondante est `out\ninja-fs-msync.log` ; aucun
resultat n'etait presume avant sa completion. Elle s'est terminee avec
7 actions `FAILED` ; la verification suivante `out\ninja-fs-page-final.log`
termine avec 5 actions `FAILED`, contre 9 a la reprise. getpagesize expose
la taille de page native et son test a passe dans `fs-msync-page-vm`.
La garde de variable BSD dans l'adaptation de titre a ete corrigee sans
desactiver le warning. `fs-msync-bounds-vm` passe le lot fleet qemu64 avec
le refus des backings externes non identifies au sync.
Les cinq actions restantes sont les trois sources de signaux/processus,
discardable_shared_memory (reclamation MADV_FREE) et file_util_posix.
Le dernier include grp.h inutilise masquait des contrats additionnels :
operations relatives a un FD de repertoire, no-follow, chemins canoniques,
rename, chmod et liens symboliques. Aucun de ces contrats n'est annonce
supporte par une declaration seule. L'archive Base finale et les smokes
restent non lies/non executes.

Le backend processus OS_ALOS est desormais separe des trois implementations
POSIX de launch/process/kill. Il n'installe ni masque de signal ni handler,
et ne decode pas les statuts ALOS avec WIFEXITED/WTERMSIG. Les primitives
privees query/wait/terminate exposent des PID natifs, le statut brut et une
cause native NORMAL/FORCED distincte d'un numero de signal. Wait bloque sur
la wait queue du parent, conserve l'enfant si la copie du resultat echoue
et ne reap qu'apres nettoyage de tous ses threads. Query/terminate sont
limites au processus courant et/ou a ses enfants directs ; aucun privilege
Unix, groupe de processus ou autorisation par uid n'est invente.
`native-process-vm` passe le lot fleet qemu64 avec maintien du waitpid brut,
timeout, terminaison de threads CPU-bound/bloques et rollback EFAULT.
`native-process-max-vm` passe aussi le runtime sous max et trois C++ runs.

Le lanceur Base natif vise les ELF statiques a chemin absolu depuis un
parent mono-thread. Il remappe les FD avec l'algorithme Chromium reel et
utilise un pipe CLOEXEC pour distinguer les erreurs fork/exec. L'environnement
est herite ou explicitement vide ; les overrides, groupes, rlimit setters
et delegates pre-exec sont refuses avec erreur explicite. Stdin est herite
dans ce profil, sans faux /dev/null. Les titres, waits et operations natives
restent isoles pour permettre un futur backend POSIX sans changer l'ABI
brute validee. Le smoke `base-process-smoke.cc` et les trois backends passent
leurs commandes de compilation exactes, mais le smoke n'est pas encore lie
ou execute.

`out\ninja-native-process.log` termine avec deux actions FAILED :
file_util_posix et discardable_shared_memory. Le refus de Purge a ensuite
ete explicite sur ALOS avant toute modification de l'etat partage, avec
ENOTSUP et diagnostic : aucune constante MADV_FREE ni liberation physique
simulee n'est ajoutee. Ce correctif de capacite et son smoke restent a
valider dans la prochaine compilation globale.

`out\ninja-native-process-capabilities.log` a ensuite valide ces selections
et termine avec une seule action FAILED, `file_util_posix.o`. Cela ne
represente pas une seule API manquante : ce fichier exige encore le resolveur
ancre sur un FD (openat/fstatat/unlinkat et no-follow), la canonicalisation,
rename, les permissions et liens symboliques, ainsi que la limite de nom
de composant. La purge partagee reste explicitement indisponible avec erreur,
sans que sa compilation soit presentee comme une reclamation implementee.
Le noyau/libc final du backend processus passe aussi la reconstruction
complete et les regressions dans `native-process-final-vm`.
Le Base process smoke et le smoke de refus de purge sont prepares dans GN,
mais ne sont pas declares lies ou executes ; l'archive finale Base attend
encore la completion du backend filesystem.

Le jalon filesystem suivant a leve cette derniere action :
`out\ninja-file-util-final.log` se termine avec **0 actions FAILED** et
`[1658/1658] AR obj/base/libbase.a`. L'archive thin contient 397 membres
reels, dont les definitions upstream de TimeNow/TimeTicksNow ; sa petite
taille d'index n'est pas celle des objets qu'elle reference.
Le test natif `path-ops-test` et le runtime/fleet qemu64 dans `path-ops-vm`
valident les operations relatives a un FD de repertoire, independantes du
cwd, openat/fstatat/unlinkat et O_NOFOLLOW. realpath valide d'abord la
traversee du filesystem avant de normaliser les composants. Les liens
symboliques courts Ext2 sont crees/lus, mais leur suivi reste ENOTSUP.
rename fonctionne dans le meme repertoire vers un nom inexistant tenant
dans l'enregistrement ; remplacement, deplacement de repertoire et
transaction inter-repertoires sont refuses avant mutation. chmod retourne
ENOTSUP sans changer les bits : l'identite et les permissions ne sont pas
encore enforcees. Un repertoire ouvert puis supprime reste accessible
jusqu'a fermeture, sans permettre de nouvelles creations dedans.

La premiere edition de liens du vrai `alos_native:base_smoke` a ensuite
revele des dependances runtime absentes, distinctes de la compilation Base :
protection de pile, ceilf/round/log/exp/frexp/ldexp et CollectStackTrace de
PartitionAlloc. Le wrapper natif emploie directement le ld.lld epingle de
Chromium, sans passer par le g++/CRT Linux de l'hote. Le toolchain est
desormais conserve dans le depot et le script refuse d'ecraser une version
externe divergente.

La libc fournit une garde globale par processus initialisee par getentropy
avant les tableaux preinit/init. Le hook CRT est faible : les anciens
executables non proteges ne demandent pas d'entropie ; les references fortes
des objets proteges extraient le runtime de l'archive. Aucun seed fixe ou
fallback n'est utilise. Une source absente ou une garde nulle termine le
processus avec diagnostic et statut natif 134 avant les constructeurs.
Une corruption de garde termine le groupe de threads sans destructeurs.
Fork copie la garde ; exec en initialise une nouvelle, sans revendication
de canary par thread.

Les six fonctions math requises utilisent les algorithmes binary32/binary64
de musl 1.2.6, notamment les tables/polynomes Arm MIT pour exp/log, avec
les notices conservees et adaptation errno EDOM/ERANGE. Les suites isolees
`base-math-final-vm`, `base-math-final-max-vm` et
`base-math-final-no-rng-vm` passent : zeros signes, subnormaux, NaN/Inf,
points de reference, arrondi dirige x87/SSE et exceptions invalid/divzero ;
constructeurs proteges, fork/corruption et refus d'exec protege sans RNG.
La tentative complete `stack-protector-vm` est restee incomplete apres
`threads-test`. Une nouvelle execution `base-runtime-final-vm` passe ensuite
la suite runtime/fleet complete qemu64, avec les nouveaux tests de math et
de protection de pile ; aucun correctif scheduler/TLS/xstate n'a ete ajoute
pour masquer cette interruption.

`base-smoke-secure-link.log` se termine maintenant par
`[1658/1658] LINK ./base_smoke`, sans action FAILED. readelf confirme un
ELF64 x86-64 EXEC statique, entree 0x400000, segment TLS et aucun interpreteur
dynamique ; nm ne trouve aucun symbole indefini. Le script de liens ALOS
conserve son segment ELF RWE existant : ce jalon n'apporte pas de protection
W^X des segments ELF. L'archive Base conserve ses 397 membres.
Les executions reelles `chromium-base-time-vm` (qemu64) et
`chromium-base-time-max-vm` (max) affichent
`chromium-base-smoke: Time PASS` puis `vm-suite-complete` : le programme
appelle les implementations upstream Time/TimeTicks, attend 20 ms et
verifie leurs contrats. Le runner copie les executables sur un disque de
test separe, ne retire que leurs sections debug dans ces copies et verifie
les octets de staging ; il ne modifie ni les originaux ni disk.img.
Les autres smokes Base processus/IO/atomiques/filesystem restent a valider
individuellement. Ce premier lien et cette execution de vrai Base ne
constituent pas une execution de Chromium, Blink, V8, Ozone ou Mojo.

`chromium-base-five-vm` a ensuite execute cinq smokes reels : Time,
copie atomique (alignements/bornes et lecteur concurrent), comparaisons
de fichiers, traces brutes Base/PartitionAlloc et processus. Ce dernier
valide fork/exec, capture stdout, statut brut 37, timeout/terminaison et
refus d'une option de groupe de processus ; aucune emulation de signaux
POSIX n'est ajoutee. La relance globale de `ninja-base-native-final.log`
compile toujours Base sans FAILED, puis le groupe de smokes rencontre
deux echecs de lien : discardable et IO. Ils revelent les fonctions du
backend thread ALOS manquantes, floor/ceil et le lecteur ELF generique.

Les fonctions mathematiques sont maintenant regroupees dans
`src/userland/libm/libm.a`, compilee avec Clang
`--target=x86_64-unknown-none-elf -ffreestanding -nostdinc -fno-builtin`.
Le header public est `libm/include/math.h` ; le header libc existant le
redirige. Les anciens helpers float/x87, y compris frexpl utilise par
printf, ont ete deplaces, sans definitions doubles dans libc.a.
Les liens userland utilisent un groupe libc/libm et le toolchain Chromium
lie explicitement l'archive cible. Les smokes declarent aussi les archives
runtime comme entrees GN pour qu'une modification provoque une relance.
Les notices musl/Arm sont conservees dans les sources et
`src/userland/libm/COPYRIGHT.musl`.

L'audit nm des objets Base/Abseil generes trouve ceil, ceilf, exp, floor,
frexp/frexpl, ldexp/ldexpf/ldexpl, log, round et pow. floor/ceil musl et
ldexpl sur le scalbnl x87 natif sont ajoutes aux routines deja utilisees.
pow apparait dans des objets mais n'est pas encore exige par les liens
smoke observes ; aucune approximation ni support general de libm n'est
revendique. sqrt/trunc/fmod binary64 ne sont pas ajoutes sans demande
effective ; fmodl est le helper x87 preexistant.
`libm-runtime-qemu64-vm` passe la reconstruction et la suite runtime/fleet
complete, notamment floor/ceil positifs/negatifs, zeros signes, subnormaux,
NaN/Inf et arrondi dirige, puis les tests CRT, libc, pthread et processus.
`libm-runtime-max-vm` passe aussi la suite complete avec trois executions
de complex-cpp-test relie a libc/libm/libc++ cibles, et
`libm-runtime-no-rng-vm` passe le runtime/fleet en absence de RNG, y compris
le refus de demarrage de l'executable protege.

Le backend `platform_thread_alos.cc` utilise les cinq classes existantes
du scheduler via alos_thread_set_nice : background 19, utility 10, default 0,
display-critical -5 et interactive -10. Il ne compile pas le backend Linux
et n'utilise ni uid/RLIMIT_NICE ni setpriority POSIX, toujours ENOTSUP.
Realtime audio est refuse lors de creation et de changement de type.
Le type Base en TLS n'est mis a jour qu'apres un changement natif reussi ;
les erreurs d'attributs pthread sont aussi propagees avant creation.
Les noms restent les metadonnees du gestionnaire de noms Base, sans
pretendre renommer le thread dans le noyau. La pile utilisateur par defaut
est au moins 2 MiB via les attributs pthread reels ; TLS/xstate et nettoyage
restent ceux du noyau et du trampoline pthread/CRT existants.

`ninja-libm-native-thread.log` leve les dependances thread et math de
discardable/IO : sept executables smoke sont lies, dont thread et
discardable. Il reste un echec de lien IO, avec trois fonctions de lecture
ELF (ReadElfBuildId, GetRelocationOffset, GetElfProgramHeaders).
Le lecteur concerne est un parseur upstream de buffers ELF entierement
mappes, pas un backend Linux de chargement. Son integration GN et les types
ELF64 Dyn/Nhdr sont prepares, avec un smoke de notes/build-id en memoire ;
dladdr et la decouverte de modules charges restent explicitement ENOTSUP.
`ninja-libm-elf-final.log` confirme ensuite la commande exacte
`ninja -C out/alos -k 0 -j 8 base:base` avec 0 actions FAILED, puis
`alos_native:all` avec les neuf editions de liens reussies. GN produit
maintenant 1890 targets (deux nouveaux smokes par rapport aux 1888).
L'archive Base contient 399 membres apres ajout du backend thread et du
lecteur ELF. Les neuf ELF sont statiques, sans interpreteur ni section
DYNAMIC et sans symbole indefini ; aucun CRT/libc/libm hote n'est lie.

`chromium-base-nine-vm` et `chromium-base-nine-max-vm` passent le lot
complet sous ALOS qemu64/max, avec tous les marqueurs et vm-suite-complete.
Le smoke thread verifie les cinq valeurs de nice natives, la preservation
du type/nice apres refus realtime, creation/join, nom Base, pile reelle
d'au moins 2 MiB et refus EINVAL d'une pile invalide avant demarrage.
Le smoke ELF verifie tailles ABI, en-tetes, relocation, build-id GNU
majuscules/minuscules sur un buffer valide, magic invalide et dladdr
explicitement indisponible. Il ne decouvre pas les modules de l'executable.
Le smoke discardable verifie que Purge refuse ENOTSUP sans perdre la
residence ni le contenu. Le smoke IO execute les assertions upstream/
natives de surveillance de FD, rearmement/cancellation, boucles imbriquees,
taches differees et reveils inter-thread. Les smokes Time, atomique,
comparaisons de fichiers, traces brutes et processus restent PASS.

La tentative precedente `chromium-base-seven-vm` a expire pendant les
comparaisons de fichiers, sans assertion FAIL ni fin de lot ; elle n'est
pas presentee comme une validation. Le lot final est relance sans build
concurrent avec un delai runner plus large, sans supprimer les assertions
ni modifier les deadlines internes des tests.
Aucune erreur de compilation/lien ne reste dans ce groupe de neuf smokes.
Cela ne valide pas base_unittests, l'ensemble des APIs Base, les signaux,
la decouverte de modules, les garanties realtime ou une libm complete.
Les besoins navigateur au-dela de ces smokes (connexion TCP/DNS complete,
Ozone, Blink, V8 et integration multiprocessus du navigateur) restent des
jalons distincts non demontres.

Le transport local a ensuite franchi un jalon reel. ALOS fournit maintenant
des paires AF_UNIX SOCK_STREAM anonymes, sendmsg/recvmsg, SCM_RIGHTS,
CLOEXEC/non-bloquant, EOF/HUP/EPIPE et attente poll multi-source, en reutilisant
les files IPC bornees sans presenter l'ancien protocole IPC nomme comme Unix.
Le test natif `unix-socket-test` couvre notamment le transfert d'un backing
SHM entre processus, le partage d'offset de fichier, les erreurs EMFILE,
la troncature de controle, le refus de cycles de droits et la liberation
iterative d'une chaine acyclique de 128 transferts.

Le 2026-10-01, apres ajout du chemin epoll natif, les suites
`test-vm.ps1 -Fleet -Cpu qemu64` et `-Cpu max` terminent avec
`ALOS VM suite PASS`, y compris `epoll-test: PASS`. La suite
`test-vm.ps1 -DnsOnly -Cpu qemu64` termine egalement PASS avec le resolver
getaddrinfo/DNS reel.

La compilation Chromium locale sur le checkout epingle 140.0.7339.80 confirme
zero erreur pour `base:base`, 89 actions reussies pour `alos_native:all`,
et 94 actions reussies jusqu'au LINK final de
`alos_mojo:mojo_ipcz_smoke`. Ces resultats ne constituent pas encore un build
du navigateur complet ni une validation de Blink/V8/Ozone.

Le smoke `mojo_ipcz_smoke` utilise le chemin Chromium default-ipcz reel :
le parent lance un enfant, transmet l'endpoint de plateforme, envoie une
invitation et un message contenant un SharedBufferHandle de 4096 octets.
L'enfant mappe le buffer, modifie un octet, renvoie un acquittement puis attend
la confirmation finale ; le parent observe la modification avant la fermeture
du pair. Les marqueurs parent/enfant et la sortie PASS ont ete observes sous
ALOS. Ce jalon ne valide pas les sockets Unix nommes, les credentials de pair,
la sandbox, l'ensemble des tests Mojo ni un navigateur Chromium complet.

Le front reseau actif est maintenant en cours : le noyau possede un chemin
SYN sortant natif et un etat SYN_SENT exploite par connect(), avec completion
par poll/SO_ERROR pour les sockets non bloquants. Ce chemin doit encore etre
revalide dans la flotte QEMU et ne doit pas etre presente comme fournissant
des retransmissions/temporisations TCP completes. Le resolver libc getaddrinfo dispose maintenant d'un chemin IPv4 A vers le
resolver DNS ALOS via un syscall dedie. Le resolver noyau serialise son unique
requete en vol, bloque sur une wait queue avec delai borne et refait un envoi
court pour laisser ARP amorcer la route DNS. Les chemins numeriques et
localhost restent locaux a la libc. Le mode optionnel test-vm.ps1 -DnsOnly
execute inet-test --dns contre example.com sans rendre la flotte deterministe
dependante d'un DNS externe. Cette integration reste a confirmer sous QEMU ;
IPv6, services nommes et resolution inverse POSIX complete restent hors profil.

### Bindings C++ Mojo generes : premier appel type natif

Le 2026-10-01, `mojo-current-next-vm` revalide le binaire ipcz reconstruit
par l'utilisateur avant d'ouvrir le jalon des bindings. Le graphe upstream
des bindings chargeait aussi des targets JavaScript demandant `v8/gni/v8.gni`.
Le profil opt-in `build-chromium.ps1 -MojoBindings` active
`alos_mojom_cpp_only=true` : generation C++ uniquement, sans Java/JS/TS,
sans compiler V8, Blink ou un navigateur. Le profil par defaut reste inchange.
Les typemaps natifs, les sources de validation et les assertions upstream
restent actifs ; aucune API kernel/libc ni faux support POSIX n'est ajoute.

La compilation du vrai target `mojo/public/cpp/bindings:bindings` termine
avec 77 actions reussies jusqu'a `AR .../libbindings.a`. Le nouveau
`alos_mojo:mojo_bindings_smoke` utilise `echo.mojom`, genere par les outils
upstream, et les vrais `mojo::Remote` / `mojo::Receiver` sur default-ipcz.
GN genere 2114 targets depuis 415 fichiers. Le dernier build
`ninja-mojo-typed-final.log` termine par `LINK ./mojo_bindings_smoke`, sans
FAILED. L'archive bindings contient 28 membres ; le smoke est un ELF64
x86-64 EXEC statique, entree 0x400000, sans symboles indefinis, interpreteur
ou segment DYNAMIC. La relance exacte `base:base` repond no work to do.

`mojo-typed-qemu64-vm` et `mojo-typed-max-vm` terminent PASS avec
`chromium-mojo-bindings-smoke: child typed request PASS`,
`chromium-mojo-bindings-smoke: PASS (generated C++, default ipcz)` et
`vm-suite-complete`. Le parent lance l'enfant avant de creer des threads,
envoie une invitation et appelle Exchange avec une chaine et un handle de
buffer partage de 4096 octets. L'enfant verifie la requete, mappe le buffer,
modifie l'octet 19 de 73 a 91 et repond avec une chaine et la valeur 91.
Le parent verifie le callback et la coherence de son mapping, attend Finish,
ferme son Remote ; l'enfant observe la deconnexion puis sort avec statut 0.

Reproduction avec le profil libc++ localise valide :

```powershell
.\ports\chromium\scripts\build-chromium.ps1 `
  -ChromiumDirectory $Chromium -LibcxxDirectory $Libcxx `
  -MojoBindings -Targets alos_mojo:mojo_bindings_smoke -KeepGoing -Jobs 8
.\ports\chromium\scripts\test-vm.ps1 -SkipBuild -MojoBindings `
  -MojoSmokePath "$Chromium\out\alos\mojo_bindings_smoke" -Cpu qemu64
```

Les nouveaux fichiers du graphe ont ete disponibles dans le checkout
Chromium enrichi de cette machine. Ce resultat ne prouve pas que le fetch
sparse minimal fournit deja toutes les dependances du generateur.
Les suites kernel/runtime ne sont pas refaites pour ce jalon C++ uniquement.
Cela ne valide ni l'ensemble des bindings/tests Mojo, ni les interfaces du
navigateur, ni JavaScript/V8, les sockets nommes ou les credentials de pair.
