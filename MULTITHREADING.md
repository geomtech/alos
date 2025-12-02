# Multithreading dans ALOS

## ✅ Multitasking Kernel - Basic

| Composant | Status | Fichier |
|-----------|--------|---------|
| **Scheduler Round-Robin** | ✅ | `process.c` |
| **Context Switch** | ✅ | `switch.s` |
| **Kernel Threads** | ✅ | `create_kernel_thread()` |
| **Liste circulaire de processus** | ✅ | `process_t` avec `next/prev` |
| **Timer-based preemption** | ✅ | `timer_handler_c()` appelle `schedule()` |
| **CTRL+C pour tuer les tâches** | ✅ | `kill_all_user_tasks()` |
| **Commande `tasks`** | ✅ | Lance 2 threads de test |
| **Commande `ps`** | ✅ | Liste les processus |

## ✅ Multithreading Avancé (inspiré de alix-main)

| Composant | Status | Fichier |
|-----------|--------|---------|
| **Thread séparé de Process** | ✅ | `thread.h`, `thread.c` |
| **Priorités de threads** | ✅ | 5 niveaux (IDLE → UI) |
| **Wait Queues** | ✅ | `wait_queue_t` avec `wait/wake_one/wake_all` |
| **Spinlocks** | ✅ | `spinlock_t` avec `lock/unlock/trylock` |
| **Scheduler par priorité** | ✅ | Run queues par niveau de priorité |
| **Sleep Queue** | ✅ | `thread_sleep_ticks()`, `thread_sleep_ms()` |
| **Time Slices** | ✅ | Quota de ticks par thread |
| **Thread Join** | ✅ | `thread_join()` pour attendre la fin |
| **Thread Kill** | ✅ | `thread_kill()` pour tuer un thread |
| **Commande `threads`** | ✅ | Test avec 3 threads de priorités différentes |
| **Context Switch CR3-safe** | ✅ | Skip CR3 reload si new_cr3 == 0 |
| **Préemption Kernel** | ✅ | IRQ Timer + `scheduler_preempt()` |
| **Preempt disable/enable** | ✅ | Protection sections critiques |
| **Format unifié context** | ✅ | `interrupt_frame_t` + `popa/iretd` |

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      SCHEDULER                               │
├─────────────────────────────────────────────────────────────┤
│  Priority Queues (Round-Robin par niveau)                   │
│  ┌─────────┬─────────┬─────────┬─────────┬─────────┐       │
│  │  IDLE   │BACKGRND │ NORMAL  │  HIGH   │   UI    │       │
│  │ (0)     │  (1)    │  (2)    │  (3)    │  (4)    │       │
│  └────┬────┴────┬────┴────┬────┴────┬────┴────┬────┘       │
│       │         │         │         │         │             │
│       ▼         ▼         ▼         ▼         ▼             │
│    [T1]──►   [T2]──►   [T3]──►   [T4]──►   [T5]──►         │
│       ◄──[T6]   ◄──[T7]   ◄──[T8]                          │
├─────────────────────────────────────────────────────────────┤
│  Sleep Queue: threads en attente de réveil (ticks)          │
│  Wait Queues: threads bloqués sur conditions                │
└─────────────────────────────────────────────────────────────┘
```

### Niveaux de Priorité

| Priorité | Valeur | Usage |
|----------|--------|-------|
| `THREAD_PRIORITY_IDLE` | 0 | Tâches de fond, économie CPU |
| `THREAD_PRIORITY_BACKGROUND` | 1 | Travaux en arrière-plan |
| `THREAD_PRIORITY_NORMAL` | 2 | Threads par défaut |
| `THREAD_PRIORITY_HIGH` | 3 | Tâches importantes |
| `THREAD_PRIORITY_UI` | 4 | Interface utilisateur, réactivité maximale |

### États d'un Thread

```
                    ┌──────────┐
         create()   │  READY   │◄────────────────┐
                    └────┬─────┘                 │
                         │                       │
                    schedule()              wake/timeout
                         │                       │
                         ▼                       │
                    ┌──────────┐           ┌─────┴────┐
                    │ RUNNING  │──sleep──►│ SLEEPING │
                    └────┬─────┘           └──────────┘
                         │                       ▲
                    exit()                  wait()
                         │                       │
                         ▼                 ┌─────┴────┐
                    ┌──────────┐           │ BLOCKED  │
                    │  ZOMBIE  │           └──────────┘
                    └──────────┘
```

### Structures Clés (thread.h)

```c
typedef enum {
    THREAD_PRIORITY_IDLE = 0,
    THREAD_PRIORITY_BACKGROUND,
    THREAD_PRIORITY_NORMAL,
    THREAD_PRIORITY_HIGH,
    THREAD_PRIORITY_UI,
    THREAD_PRIORITY_COUNT
} thread_priority_t;

typedef enum {
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_SLEEPING,
    THREAD_STATE_ZOMBIE
} thread_state_t;

typedef struct thread {
    uint32_t tid;
    char name[32];
    thread_state_t state;
    thread_priority_t priority;
    
    uint32_t esp;                    // Stack pointer sauvegardé
    uint32_t *stack_base;            // Base de la stack
    uint32_t stack_size;
    
    uint64_t wake_time;              // Pour sleep
    int exit_status;
    
    struct process *owner;           // Process parent
    struct thread *next_in_process;  // Liste dans le process
    struct thread *next;             // Liste globale / wait queue
    struct thread *prev;
} thread_t;

typedef struct wait_queue {
    thread_t *head;
    thread_t *tail;
    spinlock_t lock;
} wait_queue_t;
```

### API Threads

```c
// === Création / Destruction ===
thread_t *thread_create(const char *name, thread_entry_t entry, void *arg, 
                        uint32_t stack_size, thread_priority_t priority);
void thread_exit(int status);
int thread_join(thread_t *thread);
bool thread_kill(thread_t *thread, int status);

// === Contrôle ===
void thread_yield(void);
void thread_sleep_ms(uint32_t ms);
void thread_sleep_ticks(uint64_t ticks);

// === Synchronisation ===
void spinlock_init(spinlock_t *lock);
void spinlock_lock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);
bool spinlock_trylock(spinlock_t *lock);

void wait_queue_init(wait_queue_t *queue);
void wait_queue_wait(wait_queue_t *queue, wait_queue_predicate_t pred, void *ctx);
void wait_queue_wake_one(wait_queue_t *queue);
void wait_queue_wake_all(wait_queue_t *queue);

// === Scheduler ===
void scheduler_init(void);
void scheduler_schedule(void);
void scheduler_tick(void);  // Appelé par le timer (préemption désactivée)
```

### Comment Tester

```bash
make run
# Dans le shell ALOS:
threads
```

Résultat attendu :
```
=== New Multithreading Test ===
Testing thread priorities:
  H = HIGH priority (UI)
  N = NORMAL priority
  L = LOW (background) priority

Threads created! Output: HNLHNLHNLHNLHNL...

Results:
  High priority iterations: 5
  Normal priority iterations: 5
  Low priority iterations: 5
All threads completed!
```

## 🔄 En Cours / À Faire

### Synchronisation Avancée
- [ ] **Mutex** - Verrouillage exclusif avec owner tracking
- [ ] **Semaphores** - Compteurs pour ressources limitées
- [ ] **Condition Variables** - Attente sur conditions complexes
- [ ] **Read-Write Locks** - Lecteurs multiples, écrivain exclusif

### ✅ Préemption Automatique (Implémenté!)
- [x] IRQ Timer avec sauvegarde contexte complet (`interrupt_frame_t`)
- [x] `scheduler_preempt()` appelé depuis IRQ0
- [x] `preempt_disable()` / `preempt_enable()` pour sections critiques
- [x] Format unifié `popa + iretd` pour tous les context switches
- [x] Time slice épuisé → préemption automatique

### Thread-Safety Kernel
- [ ] Protéger `kmalloc()` avec spinlock
- [ ] Protéger structures du scheduler
- [ ] Protéger console/serial output
- [ ] Atomic operations (`atomic_inc`, `atomic_dec`, `atomic_cmpxchg`)

### Améliorations Scheduler
- [ ] **Aging** - Éviter famine des threads basse priorité
- [ ] **Nice values** - Ajustement fin des priorités
- [ ] **CPU time accounting** - Mesurer le temps CPU par thread
- [ ] **Load balancing** - Pour futur SMP

### Kernel Threads Utiles
- [x] **Idle thread** - `hlt` pour économie d'énergie
- [ ] **Reaper thread** - Nettoyage des threads zombie
- [ ] **Worker threads** - Pool pour travaux asynchrones

## ❌ User Mode Multithreading (Non implémenté)

Pour avoir plusieurs programmes ELF en parallèle en User Mode :

| Fonctionnalité | Status | Description |
|----------------|--------|-------------|
| Isolation mémoire | ❌ | Chaque process = son propre Page Directory |
| `exec` non-bloquant | ❌ | Actuellement `exec` attend la fin du programme |
| Context switch Ring 3 | ❌ | Sauvegarder/restaurer contexte User Mode |
| `fork()` / `spawn()` | ❌ | Créer des processus enfants |
| Signaux | ❌ | Communication inter-processus |

### Situation Actuelle

```
┌─────────────────────────────────────────┐
│            Kernel Mode (Ring 0)          │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐    │
│  │ Thread1 │ │ Thread2 │ │ Thread3 │    │  ← Parallèle ✅
│  └─────────┘ └─────────┘ └─────────┘    │
├─────────────────────────────────────────┤
│            User Mode (Ring 3)            │
│  ┌──────────────────────────────────┐   │
│  │     Programme ELF (bloquant)      │   │  ← Séquentiel ❌
│  └──────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

- Les **threads kernel** peuvent tourner en parallèle ✅
- Les **programmes user** sont exécutés un par un (bloquant) ❌

## Fichiers Clés

| Fichier | Description |
|---------|-------------|
| `src/kernel/thread.h` | Structures et API threads + `interrupt_frame_t` |
| `src/kernel/thread.c` | Implémentation scheduler + threads + préemption |
| `src/kernel/process.c` | Gestion des processus |
| `src/arch/x86/switch.s` | Context switch assembleur (`switch_context`) |
| `src/arch/x86/interrupts.s` | IRQ handlers avec support préemption |
| `src/kernel/timer.c` | Timer + `timer_handler_preempt()` |
| `src/shell/commands.c` | Commande `threads` de test |

## Historique des Bugs Corrigés

| Bug | Cause | Fix |
|-----|-------|-----|
| Triple fault sur `threads` | `switch_task` chargeait CR3=0 | Skip CR3 reload si new_cr3 == 0 |
| Pas de thread main | Shell sans `thread_t` associé | Créer main_thread dans `scheduler_init` |
| Format stack incompatible | `switch_task` vs `popa+iretd` | Format unifié `interrupt_frame_t` |