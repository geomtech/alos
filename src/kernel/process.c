/* src/kernel/process.c - Process/Thread Management Implementation */
#include "process.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/usermode.h"
#include "../fs/vfs.h"
#include "../include/string.h"
#include "../mm/kheap.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../mm/vm.h"
#include "../arch/x86_64/xstate.h"
#include "../arch/x86_64/cpu.h"
#include "thread_lifecycle.h"
#include "console.h"
#include "elf.h"
#include "keyboard.h"
#include "klog.h"
#include "thread.h"
#include "workqueue.h"
#include "input.h"
#include "uaccess.h"
#include "../include/errno.h"

/* ========================================
 * Constantes
 * ======================================== */

/* Use KERNEL_STACK_SIZE from process.h (32 KiB) */

/* User stack layout comes from process.h -> memlayout.h. */

/* ========================================
 * Variables globales
 * ======================================== */

/* Processus actuellement en cours d'exécution */
process_t *current_process = NULL;

/* Liste de tous les processus (tête de la liste circulaire) */
process_t *process_list = NULL;

/* Processus idle (le kernel main) */
process_t *idle_process = NULL;

/* Compteur de PID */
static uint32_t next_pid = 0;

/* Flag pour savoir si le multitasking est actif */
static int multitasking_enabled = 0;

/* Déclaration externe du point d'entrée ASM */
extern void task_entry_point(void);

/* ========================================
 * Fonctions utilitaires
 * ======================================== */

/**
 * Copie une chaîne de manière sécurisée.
 */
static void safe_strcpy(char *dest, const char *src, uint32_t max_len) {
  uint32_t i;
  for (i = 0; i < max_len - 1 && src[i]; i++) {
    dest[i] = src[i];
  }
  dest[i] = '\0';
}

static void process_inherit_cwd(process_t *proc, const process_t *parent) {
  const char *cwd =
      (parent != NULL && parent->cwd[0] == '/') ? parent->cwd : "/";
  safe_strcpy(proc->cwd, cwd, sizeof(proc->cwd));
}

static uint64_t process_lock(void) {
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
  return flags;
}

static void process_unlock(uint64_t flags) {
  asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

static void process_link(process_t *parent, process_t *proc) {
  uint64_t flags = process_lock();

  proc->next = parent->next;
  proc->prev = parent;
  parent->next->prev = proc;
  parent->next = proc;

  proc->parent = parent;
  proc->sibling_prev = NULL;
  proc->sibling_next = parent->first_child;
  if (parent->first_child != NULL) {
    parent->first_child->sibling_prev = proc;
  }
  parent->first_child = proc;

  process_unlock(flags);
}

static void process_unlink(process_t *proc) {
  if (proc->parent != NULL) {
    if (proc->sibling_prev != NULL) {
      proc->sibling_prev->sibling_next = proc->sibling_next;
    } else {
      proc->parent->first_child = proc->sibling_next;
    }
    if (proc->sibling_next != NULL) {
      proc->sibling_next->sibling_prev = proc->sibling_prev;
    }
  }

  if (proc->next != NULL && proc->prev != NULL) {
    proc->prev->next = proc->next;
    proc->next->prev = proc->prev;
    if (process_list == proc) {
      process_list = proc->next != proc ? proc->next : NULL;
    }
  }

  proc->parent = NULL;
  proc->sibling_next = NULL;
  proc->sibling_prev = NULL;
  proc->next = NULL;
  proc->prev = NULL;
}

int process_resolve_path(const char *path, char *resolved, size_t size) {
  if (path == NULL || resolved == NULL || size < 2 || path[0] == '\0') {
    return -1;
  }

  size_t out_len = 1;
  resolved[0] = '/';
  resolved[1] = '\0';

  if (path[0] != '/') {
    process_t *proc = process_current();
    if (proc == NULL || proc->cwd[0] != '/') {
      return -1;
    }

    size_t cwd_len = 0;
    while (cwd_len < PROCESS_CWD_MAX && proc->cwd[cwd_len] != '\0') {
      cwd_len++;
    }
    if (cwd_len == PROCESS_CWD_MAX || cwd_len >= size) {
      return -1;
    }

    for (size_t i = 0; i <= cwd_len; i++) {
      resolved[i] = proc->cwd[i];
    }
    out_len = cwd_len;
  }

  size_t pos = 0;
  while (pos < PROCESS_CWD_MAX) {
    while (pos < PROCESS_CWD_MAX && path[pos] == '/') {
      pos++;
    }
    if (pos == PROCESS_CWD_MAX) {
      return -1;
    }
    if (path[pos] == '\0') {
      resolved[out_len] = '\0';
      return 0;
    }

    size_t component_start = pos;
    while (pos < PROCESS_CWD_MAX && path[pos] != '/' && path[pos] != '\0') {
      pos++;
    }
    if (pos == PROCESS_CWD_MAX) {
      return -1;
    }

    size_t component_len = pos - component_start;
    if (component_len == 1 && path[component_start] == '.') {
      continue;
    }
    if (component_len == 2 && path[component_start] == '.' &&
        path[component_start + 1] == '.') {
      while (out_len > 1 && resolved[out_len - 1] != '/') {
        out_len--;
      }
      if (out_len > 1) {
        out_len--;
      }
      resolved[out_len] = '\0';
      continue;
    }

    size_t separator_len = (out_len > 1) ? 1 : 0;
    if (out_len + separator_len + component_len >= size) {
      return -1;
    }
    if (separator_len != 0) {
      resolved[out_len++] = '/';
    }
    for (size_t i = 0; i < component_len; i++) {
      resolved[out_len++] = path[component_start + i];
    }
    resolved[out_len] = '\0';
  }

  return -1;
}

/* ========================================
 * Implémentation
 * ======================================== */

void init_multitasking(void) {
  KLOG_INFO("TASK", "=== Initializing Multitasking ===");

  /* Initialiser le scheduler de threads */
  scheduler_init();

  /* Créer le processus idle (représente le kernel actuel) */
  idle_process = (process_t *)kmalloc(sizeof(process_t));
  if (idle_process == NULL) {
    KLOG_ERROR("TASK", "Failed to allocate idle process!");
    return;
  }

  /* Initialiser le processus idle */
  memset(idle_process, 0, sizeof(*idle_process));
  idle_process->pid = next_pid++;
  safe_strcpy(idle_process->name, "kernel_idle", sizeof(idle_process->name));
  idle_process->state = PROCESS_STATE_RUNNING;
  idle_process->should_terminate = 0;
  idle_process->exit_status = 0;
  idle_process->uses_framebuffer = false;

  /* L'ESP sera sauvegardé lors du premier switch */
  idle_process->rsp = 0;
  idle_process->rsp0 = 0;

  /* Utiliser le Page Directory du kernel */
  idle_process->pml4 = (uint64_t *)vmm_get_kernel_directory();
  idle_process->cr3 =
      (uint64_t)idle_process->pml4; /* Adresse physique pour CR3 */
  idle_process->heap_start = 0;
  idle_process->heap_brk = 0;
  process_inherit_cwd(idle_process, NULL);
  file_table_init(idle_process->fd_table, NULL);

  /* Pas de stack allouée (on utilise la stack du kernel) */
  idle_process->stack_base = NULL;
  idle_process->stack_size = 0;

  /* Threads */
  idle_process->main_thread = NULL;
  idle_process->thread_list = NULL;
  idle_process->thread_count = 0;

  /* Wait queue pour process_join */
  wait_queue_init(&idle_process->wait_queue);

  /* Hiérarchie */
  idle_process->parent = NULL;
  idle_process->first_child = NULL;
  idle_process->sibling_next = NULL;
  idle_process->sibling_prev = NULL;

  /* Liste circulaire : pointe vers lui-même */
  idle_process->next = idle_process;
  idle_process->prev = idle_process;

  /* Définir comme processus courant et tête de liste */
  current_process = idle_process;
  process_list = idle_process;

  /* Activer le multitasking */
  multitasking_enabled = 1;

  /* Démarrer le scheduler */
  scheduler_start();

  /* Initialize the reaper thread for zombie cleanup */
  reaper_init();

  /* Initialize the kernel worker pool */
  workqueue_init();

  KLOG_INFO("TASK", "Multitasking initialized");
  KLOG_INFO_DEC("TASK", "Idle process PID: ", idle_process->pid);
}

process_t *create_kernel_thread(void (*function)(void), const char *name) {
  if (!multitasking_enabled) {
    KLOG_ERROR("TASK", "Multitasking not initialized!");
    return NULL;
  }

  KLOG_INFO("TASK", "Creating kernel thread:");
  KLOG_INFO("TASK", name);

  /* Allouer la structure du processus */
  process_t *proc = (process_t *)kmalloc(sizeof(process_t));
  if (proc == NULL) {
    KLOG_ERROR("TASK", "Failed to allocate process structure!");
    return NULL;
  }

  /* Allouer la stack kernel pour ce thread */
  void *stack = kmalloc(KERNEL_STACK_SIZE);
  if (stack == NULL) {
    KLOG_ERROR("TASK", "Failed to allocate kernel stack!");
    kfree(proc);
    return NULL;
  }

  /* Initialiser le processus */
  memset(proc, 0, sizeof(*proc));
  proc->pid = next_pid++;
  safe_strcpy(proc->name, name, sizeof(proc->name));
  proc->state = PROCESS_STATE_READY;
  proc->should_terminate = 0;
  process_inherit_cwd(proc, current_process);
  file_table_init(proc->fd_table, current_process->fd_table);

  /* Page Directory (partagé avec le kernel pour les threads kernel) */
  proc->pml4 = (uint64_t *)vmm_get_kernel_directory();
  proc->cr3 = (uint64_t)proc->pml4; /* Threads kernel partagent le même CR3 */
  proc->heap_start = 0;
  proc->heap_brk = 0;

  /* Stack */
  proc->stack_base = stack;
  proc->stack_size = KERNEL_STACK_SIZE;

  /* ========================================
   * Préparer la stack initiale
   * ========================================
   *
   * La stack doit ressembler à celle d'un processus interrompu
   * par switch_task(). Quand on fera switch_task vers ce processus,
   * il "reprendra" comme s'il avait été interrompu.
   *
   * Layout de la stack (du haut vers le bas, adresses décroissantes):
   *
   * [stack_top - 0]  : (Libre)
   * [stack_top - 4]  : Adresse de la fonction (sera pop par task_entry_point)
   * [stack_top - 8]  : Adresse de retour (task_entry_point)
   * [stack_top - 12] : EBX (sauvegardé)
   * [stack_top - 16] : ESI (sauvegardé)
   * [stack_top - 20] : EDI (sauvegardé)
   * [stack_top - 24] : EBP (sauvegardé) <- ESP initial
   */

  uint64_t *stack_top = (uint64_t *)((uint64_t)stack + KERNEL_STACK_SIZE);

  /* Placer l'adresse de la fonction thread */
  *(--stack_top) = (uint64_t)function; /* Adresse de la fonction */

  /* Placer l'adresse de task_entry_point comme "adresse de retour" */
  *(--stack_top) = (uint64_t)task_entry_point; /* EIP - où ret sautera */

  /* Placer les registres callee-saved (initialisés à 0) */
  *(--stack_top) = 0; /* EBX */
  *(--stack_top) = 0; /* ESI */
  *(--stack_top) = 0; /* EDI */
  *(--stack_top) = 0; /* EBP */

  /* L'ESP initial pointe vers le haut de ces registres */
  proc->rsp = (uint64_t)stack_top;
  proc->rsp0 = (uint64_t)stack + KERNEL_STACK_SIZE;

  /* ========================================
   * Ajouter à la liste circulaire
   * ======================================== */

  /* Désactiver les interruptions pendant la modification de la liste */
  asm volatile("cli");

  /* Insérer après le processus courant */
  proc->next = current_process->next;
  proc->prev = current_process;
  current_process->next->prev = proc;
  current_process->next = proc;

  /* Réactiver les interruptions */
  asm volatile("sti");

  KLOG_INFO_DEC("TASK", "Thread created with PID: ", proc->pid);
  KLOG_INFO_HEX("TASK", "Stack at: ", (uint64_t)stack);
  KLOG_INFO_HEX("TASK", "Initial ESP: ", proc->rsp);

  return proc;
}

void schedule(void) {
  /* Ne rien faire si le multitasking n'est pas actif */
  if (!multitasking_enabled || current_process == NULL) {
    return;
  }

  /* Ne rien faire s'il n'y a qu'un seul processus */
  if (current_process->next == current_process) {
    return;
  }

  /* DEBUG: This should never be reached if only idle_process exists */
  KLOG_ERROR("SCHED", "schedule() called with multiple processes!");
  KLOG_ERROR("SCHED", current_process->name);

  /* Trouver le prochain processus READY */
  process_t *next = current_process->next;
  process_t *start = next;

  do {
    if (next->state == PROCESS_STATE_READY ||
        next->state == PROCESS_STATE_RUNNING) {
      break;
    }
    next = next->next;
  } while (next != start);

  /* Si c'est le même processus, ne pas switcher */
  if (next == current_process) {
    return;
  }

  /* Effectuer le context switch */
  switch_to(next);
}

void switch_to(process_t *next) {
  if (next == NULL || next == current_process) {
    return;
  }

  /* Sauvegarder le processus actuel */
  process_t *prev = current_process;

  /* Marquer l'ancien comme READY (s'il tournait) */
  if (prev->state == PROCESS_STATE_RUNNING) {
    prev->state = PROCESS_STATE_READY;
  }

  /* Marquer le nouveau comme RUNNING */
  next->state = PROCESS_STATE_RUNNING;
  current_process = next;

  /* ========================================
   * Mettre à jour le TSS
   * ========================================
   * Mettre à jour TSS.RSP0 seulement pour les processus kernel.
   * Ne JAMAIS mettre à jour TSS.RSP0 quand on switch vers un processus user
   * qui est déjà dans le kernel (au milieu d'un syscall).
   *
   * Note: Cette fonction est legacy et ne devrait plus être utilisée
   * pour les threads user (utiliser scheduler_schedule à la place).
   */
  if (next->pml4 == (uint64_t *)vmm_get_kernel_directory() && next->rsp0 != 0) {
    tss_set_rsp0(next->rsp0);
  }

  /* Effectuer le context switch ASM avec changement de CR3 */
  /* Note: Le changement de Page Directory (CR3) est fait dans switch_task */
  /* pour garantir une transition atomique entre les espaces mémoire */
  switch_task(&prev->rsp, next->rsp, next->cr3);
}

void process_exit(void) {
  if (current_process == NULL || current_process == idle_process) {
    /* Ne pas terminer le processus idle ! */
    KLOG_ERROR("TASK", "Cannot exit idle process!");
    for (;;)
      asm volatile("hlt");
  }

  KLOG_INFO("TASK", "Process exiting:");
  KLOG_INFO("TASK", current_process->name);

  /* Désactiver les interruptions - resteront désactivées jusqu'au STI dans
   * task_entry_point */
  asm volatile("cli");

  /* Marquer comme terminé */
  current_process->state = PROCESS_STATE_TERMINATED;

  /* Retirer de la liste circulaire */
  current_process->prev->next = current_process->next;
  current_process->next->prev = current_process->prev;

  /* Sauvegarder le pointeur pour libérer la mémoire plus tard */
  process_t *old_process = current_process;

  /* Passer au processus suivant */
  process_t *next = current_process->next;
  current_process = next;
  next->state = PROCESS_STATE_RUNNING;

  /* Charger le contexte du processus suivant */
  /* Note: on ne peut pas utiliser switch_task car on n'a pas d'ancien ESP
   * valide */
  /* Les interruptions seront réactivées par le STI dans task_entry_point */
  /* ou après le ret si c'est un processus existant */
  uint64_t new_rsp = next->rsp;

  /* TODO: Ajouter old_process à une liste de "zombie" pour libération
   * ultérieure */
  /* On ne peut pas kfree() ici car on est encore sur sa stack potentiellement
   */
  (void)old_process;

  /* Restaurer le contexte x86-64 (callee-saved registers) */
  asm volatile("mov %0, %%rsp\n"
               "pop %%r15\n"
               "pop %%r14\n"
               "pop %%r13\n"
               "pop %%r12\n"
               "pop %%rbp\n"
               "pop %%rbx\n"
               "sti\n" /* Réactiver les interruptions juste avant ret */
               "ret\n"
               :
               : "r"(new_rsp));

  /* On ne devrait jamais arriver ici */
  for (;;)
    asm volatile("hlt");
}

uint32_t getpid(void) {
  if (current_process == NULL) {
    return 0;
  }
  return current_process->pid;
}

void yield(void) { schedule(); }

int should_exit(void) {
  if (current_process == NULL) {
    return 0;
  }
  return current_process->should_terminate;
}

void process_list_debug(void) {
  if (process_list == NULL) {
    console_puts("No processes.\n");
    return;
  }

  console_puts("\n=== Process List ===\n");
  console_puts("PID  State    Name\n");
  console_puts("---  -----    ----\n");

  process_t *proc = process_list;
  do {
    /* PID */
    console_put_dec(proc->pid);
    console_puts("    ");

    /* State */
    switch (proc->state) {
    case PROCESS_STATE_READY:
      console_puts("READY  ");
      break;
    case PROCESS_STATE_RUNNING:
      console_puts("RUN    ");
      break;
    case PROCESS_STATE_BLOCKED:
      console_puts("BLOCK  ");
      break;
    case PROCESS_STATE_TERMINATED:
      console_puts("TERM   ");
      break;
    default:
      console_puts("???    ");
      break;
    }
    console_puts("  ");

    /* Name */
    console_puts(proc->name);

    /* Current indicator */
    if (proc == current_process) {
      console_puts(" <-- current");
    }
    console_puts("\n");

    proc = proc->next;
  } while (proc != process_list);

  console_puts("====================\n");
}

void kill_all_user_tasks(void) {
  if (process_list == NULL || idle_process == NULL) {
    return;
  }

  /* Désactiver les interruptions pendant la modification */
  asm volatile("cli");

  /* Compter les tâches à tuer */
  int killed_count = 0;

  /* Parcourir tous les processus et marquer should_terminate */
  process_t *proc = idle_process->next;

  while (proc != idle_process) {
    process_t *next_proc = proc->next;

    /* Demander au thread de s'arrêter */
    proc->should_terminate = 1;

    /* Marquer comme terminé et retirer de la liste */
    proc->state = PROCESS_STATE_TERMINATED;
    killed_count++;

    /* Passer au suivant */
    proc = next_proc;
  }

  /* Reconstruire la liste : seulement idle */
  idle_process->next = idle_process;
  idle_process->prev = idle_process;

  /* Le processus courant DOIT être idle maintenant */
  current_process = idle_process;
  current_process->state = PROCESS_STATE_RUNNING;

  /* Réactiver les interruptions */
  asm volatile("sti");

  if (!console_is_enabled()) {
    keyboard_clear_buffer();
    console_set_enabled(true);
    console_clear(VGA_COLOR_BLACK);
  }

  if (killed_count > 0) {
    console_puts("\nKilled ");
    console_put_dec(killed_count);
    console_puts(" task(s)\n");
  }
}

/* ========================================
 * Exécution de programmes ELF (User Mode)
 * ======================================== */

/* User stack address/size are defined centrally in memlayout.h. */

/* ========================================
 * Helpers de log pour le lifecycle
 * ======================================== */

static void int_to_dec(int val, char *buf) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  int i = 0;
  bool neg = false;
  unsigned int uval;
  if (val < 0) {
    neg = true;
    uval = (unsigned int)(-val);
  } else {
    uval = (unsigned int)val;
  }
  char tmp[16];
  int t = 0;
  while (uval > 0) {
    tmp[t++] = '0' + (uval % 10);
    uval /= 10;
  }
  if (neg) {
    buf[i++] = '-';
  }
  while (t > 0) {
    buf[i++] = tmp[--t];
  }
  buf[i] = '\0';
}

void proc_log_wait_begin(uint32_t pid) {
  char buf[64];
  char num[16];
  int_to_dec((int)pid, num);
  buf[0] = '\0';
  strcpy(buf, "wait begin ");
  strcat(buf, num);
  KLOG_INFO("PROC", buf);
}

void proc_log_last_thread_exited(uint32_t pid) {
  char buf[64];
  char num[16];
  int_to_dec((int)pid, num);
  buf[0] = '\0';
  strcpy(buf, "last thread exited ");
  strcat(buf, num);
  KLOG_INFO("PROC", buf);
}

void proc_log_zombie(uint32_t pid, int status) {
  char buf[64];
  char num[16];
  char snum[16];
  int_to_dec((int)pid, num);
  int_to_dec(status, snum);
  buf[0] = '\0';
  strcpy(buf, "-> ZOMBIE ");
  strcat(buf, num);
  strcat(buf, " ");
  strcat(buf, snum);
  KLOG_INFO("PROC", buf);
}

void proc_log_waking_parent(uint32_t pid) {
  char buf[64];
  char num[16];
  int_to_dec((int)pid, num);
  buf[0] = '\0';
  strcpy(buf, "waking parent ");
  strcat(buf, num);
  KLOG_INFO("PROC", buf);
}

void proc_log_wait_completed(uint32_t pid, int status) {
  char buf[64];
  char num[16];
  char snum[16];
  int_to_dec((int)pid, num);
  int_to_dec(status, snum);
  buf[0] = '\0';
  strcpy(buf, "wait completed ");
  strcat(buf, num);
  strcat(buf, " ");
  strcat(buf, snum);
  KLOG_INFO("PROC", buf);
}

void proc_log_final_reap(uint32_t pid) {
  char buf[64];
  char num[16];
  int_to_dec((int)pid, num);
  buf[0] = '\0';
  strcpy(buf, "final reap ");
  strcat(buf, num);
  KLOG_INFO("PROC", buf);
}

/* ========================================
 * Exécution de programmes ELF (User Mode)
 * ======================================== */

/**
 * Crée et démarre un processus user mode à partir d'un fichier ELF.
 * Factorise proprement le chargement ELF, la création du Page Directory,
 * la stack user avec argc/argv, et le rattachement à la hiérarchie.
 */
/* Limites natives partagees par argv/envp, y compris leurs terminateurs. */
#define EXEC_VECTOR_MAX 64
#define EXEC_STRING_BYTES 16384

static int process_initial_stack(process_t *proc, int argc, char **argv,
                                 int envc, char **envp, uint64_t *rsp) {
  size_t total = ((size_t)argc + envc + 3) * sizeof(uint64_t);
  for (int group = 0; group < 2; ++group) {
    char **vector = group ? envp : argv;
    int count = group ? envc : argc;
    for (int i = 0; i < count; ++i) {
      size_t length = 0;
      while (length < EXEC_STRING_BYTES && vector[i][length]) ++length;
      if (length == EXEC_STRING_BYTES || total > EXEC_STRING_BYTES - length - 1)
        return -1;
      total += length + 1;
    }
  }
  uint8_t *buffer = kmalloc(total);
  if (!buffer) return -1;
  *rsp = (USER_STACK_TOP - total) & ~0xfULL;
  uint64_t *words = (uint64_t *)buffer;
  words[0] = argc;
  size_t offset = ((size_t)argc + envc + 3) * sizeof(uint64_t);
  size_t index = 1;
  for (int group = 0; group < 2; ++group) {
    char **vector = group ? envp : argv;
    int count = group ? envc : argc;
    for (int i = 0; i < count; ++i) {
      size_t length = strlen(vector[i]) + 1;
      words[index++] = *rsp + offset;
      memcpy(buffer + offset, vector[i], length);
      offset += length;
    }
    words[index++] = 0;
  }
  int result = vmm_copy_to_dir((page_directory_t *)proc->pml4, *rsp, buffer, total);
  kfree(buffer);
  return result;
}

static process_t *process_spawn_environment(const char *filename, int argc,
                                            char **argv, int envc, char **envp) {
  if (argc < 0 || argc > EXEC_VECTOR_MAX || envc < 0 || envc > EXEC_VECTOR_MAX ||
      (argc && !argv) || (envc && !envp)) return NULL;
  if (!multitasking_enabled) {
    KLOG_ERROR("EXEC", "Multitasking not initialized!");
    return NULL;
  }

  char resolved_filename[PROCESS_CWD_MAX];
  bool has_slash = false;
  for (int i = 0; filename[i] != '\0'; i++) {
    if (filename[i] == '/') {
      has_slash = true;
      break;
    }
  }

  if (!has_slash) {
    /* Nom de commande simple sans chemin (ex: 'ls', 'echo') : tester /bin/ */
    char bin_path[PROCESS_CWD_MAX];
    safe_strcpy(bin_path, "/bin/", sizeof(bin_path));
    uint32_t blen = 5;
    for (uint32_t i = 0; filename[i] && blen < sizeof(bin_path) - 1; i++) {
      bin_path[blen++] = filename[i];
    }
    bin_path[blen] = '\0';

    vfs_node_t *bin_node = vfs_resolve_path(bin_path);
    if (bin_node != NULL && (bin_node->type & VFS_FILE)) {
      safe_strcpy(resolved_filename, bin_path, sizeof(resolved_filename));
    } else {
      /* Sinon tenter résolution par rapport au CWD */
      if (process_resolve_path(filename, resolved_filename,
                               sizeof(resolved_filename)) != 0) {
        return NULL;
      }
    }
  } else {
    /* Chemin absolu ou relatif avec slash (ex: '/bin/ls', './test') */
    if (process_resolve_path(filename, resolved_filename,
                             sizeof(resolved_filename)) != 0) {
      return NULL;
    }
  }
  filename = resolved_filename;

  KLOG_INFO("EXEC", "=== Spawning Program ===");
  KLOG_INFO("EXEC", filename);

  /* Vérifier si le fichier existe et est un fichier régulier */
  vfs_node_t *exec_node = vfs_resolve_path(filename);
  if (exec_node == NULL || !(exec_node->type & VFS_FILE)) {
    KLOG_WARN("EXEC", "Executable file not found");
    return NULL;
  }

  /* Vérifier si le fichier est un ELF valide */
  if (!elf_is_valid(filename)) {
    KLOG_ERROR("EXEC", "Not a valid ELF file");
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("Error: ");
    console_puts(filename);
    console_puts(" is not a valid ELF executable\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return NULL;
  }

  /* Allouer la structure du processus */
  process_t *proc = (process_t *)kmalloc(sizeof(process_t));
  if (proc == NULL) {
    KLOG_ERROR("EXEC", "Failed to allocate process structure!");
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("Error: Failed to allocate process structure\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return NULL;
  }

  /* Allouer une stack kernel pour ce processus (pour les syscalls) */
  void *kernel_stack = kmalloc(KERNEL_STACK_SIZE);
  if (kernel_stack == NULL) {
    KLOG_ERROR("EXEC", "Failed to allocate kernel stack!");
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("Error: Failed to allocate kernel stack\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kfree(proc);
    return NULL;
  }

  /* Initialiser la kernel stack à 0 */
  memset(kernel_stack, 0, KERNEL_STACK_SIZE);

  /* Initialiser le processus */
  memset(proc, 0, sizeof(*proc));
  proc->pid = next_pid++;

  /* Extraire le nom du fichier pour le nom du processus */
  const char *name = filename;
  for (const char *p = filename; *p; p++) {
    if (*p == '/')
      name = p + 1;
  }
  safe_strcpy(proc->name, name, sizeof(proc->name));

  proc->state = PROCESS_STATE_READY;
  proc->should_terminate = 0;
  proc->exit_status = 0;
  proc->native_exit_reason = ALOS_PROCESS_EXIT_NORMAL;
  process_inherit_cwd(proc, current_process);

  /* Créer un nouveau Page Directory pour l'isolation mémoire */
  page_directory_t *dir = vmm_create_directory();
  if (dir == NULL) {
    KLOG_ERROR("EXEC", "Failed to create page directory!");
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("Error: Failed to create page directory\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    kfree(kernel_stack);
    kfree(proc);
    return NULL;
  }
  proc->pml4 = (uint64_t *)dir; /* Stocker le pointeur vers la structure */
  proc->cr3 = dir->pml4_phys;   /* CR3 = adresse PHYSIQUE du PML4 */

  KLOG_INFO_HEX("EXEC", "Created page directory at: ", proc->cr3);

  /* Stack kernel */
  proc->stack_base = kernel_stack;
  proc->stack_size = KERNEL_STACK_SIZE;
  proc->rsp0 = (uint64_t)kernel_stack + KERNEL_STACK_SIZE;

  /* Charger le fichier ELF */
  elf_load_result_t elf_result;
  int err = elf_load_file(filename, proc, &elf_result);
  if (err != ELF_OK) {
    KLOG_ERROR("EXEC", "Failed to load ELF file");
    console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    console_puts("Error: Failed to load ELF file (code ");
    console_put_dec(err);
    console_puts(")\n");
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vmm_free_directory((page_directory_t *)proc->pml4);
    kfree(kernel_stack);
    kfree(proc);
    return NULL;
  }

  KLOG_INFO_HEX("EXEC", "Entry point: ", elf_result.entry_point);

  /* Initialize heap relative to ELF top address (aligned to 4KB) */
  uint64_t heap_base = (elf_result.top_addr + 0xFFF) & ~0xFFF;
  proc->heap_start = heap_base;
  proc->heap_brk = heap_base;

  KLOG_INFO_HEX("EXEC", "Heap start: ", proc->heap_start);

  /* Allouer la stack utilisateur dans le Page Directory du processus */
  uint64_t user_stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
  for (uint64_t addr = user_stack_bottom; addr < USER_STACK_TOP;
       addr += PAGE_SIZE) {
    if (!vmm_is_mapped_in_dir((page_directory_t *)proc->pml4, addr)) {
      void *page_virt = pmm_alloc_block();
      if (page_virt == NULL) {
        KLOG_ERROR("EXEC", "Failed to allocate user stack!");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to allocate user stack (phys memory)\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vmm_free_directory((page_directory_t *)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return NULL;
      }
      uint64_t page_phys = pmm_virt_to_phys(page_virt);
      if (vmm_map_page_in_dir((page_directory_t *)proc->pml4, page_phys, addr,
                              PAGE_PRESENT | PAGE_RW | PAGE_USER |
                                  PAGE_OWNED) != 0) {
        KLOG_ERROR("EXEC", "Failed to map user stack page!");
        console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        console_puts("Error: Failed to map user stack page\n");
        console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        pmm_free_block(page_virt);
        vmm_free_directory((page_directory_t *)proc->pml4);
        kfree(kernel_stack);
        kfree(proc);
        return NULL;
      }
      memset(page_virt, 0, PAGE_SIZE);
    }
  }

  KLOG_INFO_HEX64("EXEC", "User stack top: ", USER_STACK_TOP);

  /* argc, argv[], NULL, envp[], NULL ; textes copies dans la meme stack. */
  uint64_t user_rsp;
  if (process_initial_stack(proc, argc, argv, envc, envp, &user_rsp) != 0) {
    KLOG_ERROR("EXEC", "Failed to initialize bounded argv/envp stack");
    vmm_free_directory((page_directory_t *)proc->pml4);
    kfree(kernel_stack);
    kfree(proc);
    return NULL;
  }

  KLOG_INFO_HEX("EXEC", "User ESP: ", user_rsp);

  /* Initialiser les champs */
  proc->main_thread = NULL;
  proc->thread_list = NULL;
  proc->thread_count = 0;
  proc->exit_status = 0;
  proc->native_exit_reason = ALOS_PROCESS_EXIT_NORMAL;
  proc->uses_framebuffer = false;

  wait_queue_init(&proc->wait_queue);

  /* Hiérarchie des processus */
  proc->parent = current_process;
  proc->first_child = NULL;
  proc->sibling_next = NULL;
  proc->sibling_prev = NULL;
  file_table_init(proc->fd_table, current_process->fd_table);

  /* Créer le thread user mode */
  thread_t *main_thread = thread_create_user(
      proc, proc->name, elf_result.entry_point,
      user_rsp,
      NULL,
      kernel_stack, KERNEL_STACK_SIZE);

  if (main_thread == NULL) {
    KLOG_ERROR("EXEC", "Failed to create user thread!");
    file_table_destroy(proc->fd_table);
    vmm_free_directory((page_directory_t *)proc->pml4);
    kfree(kernel_stack);
    kfree(proc);
    return NULL;
  }

  proc->main_thread = main_thread;
  proc->thread_list = main_thread;
  proc->thread_count = 1;

  /* Ne pas libérer kernel_stack ici, il appartient au thread */
  proc->stack_base = NULL;

  process_link(current_process, proc);

  KLOG_INFO_DEC("EXEC", "Process created with PID: ", proc->pid);
  KLOG_INFO_DEC("EXEC", "Main thread TID: ", main_thread->tid);

  return proc;
}

/**
 * Exécute un programme ELF en créant un nouveau processus User Mode (non-bloquant).
 *
 * @param filename  Chemin du fichier ELF à exécuter
 * @return          PID du nouveau processus, ou -1 si erreur
 */
int process_execute(const char *filename) {
  process_t *proc = process_spawn(filename, 0, NULL);
  if (!proc) {
    return -1;
  }
  thread_yield();
  return (int)proc->pid;
}

process_t *process_spawn(const char *filename, int argc, char **argv) {
  return process_spawn_environment(filename, argc, argv, 0, NULL);
}

int process_fork(const interrupt_frame_t *frame) {
  process_t *parent = process_current();
  if (parent == NULL || parent == idle_process || frame == NULL ||
      parent->thread_count != 1 || parent->pml4 == NULL) {
    return -1;
  }

  process_t *child = (process_t *)kmalloc(sizeof(*child));
  if (child == NULL) {
    return -1;
  }
  memset(child, 0, sizeof(*child));

  page_directory_t *child_dir =
      vmm_clone_directory((page_directory_t *)parent->pml4);
  if (child_dir == NULL) {
    kfree(child);
    return -1;
  }

  void *kernel_stack = kmalloc(KERNEL_STACK_SIZE);
  if (kernel_stack == NULL) {
    vmm_free_directory(child_dir);
    kfree(child);
    return -1;
  }
  memset(kernel_stack, 0, KERNEL_STACK_SIZE);

  child->pid = next_pid++;
  safe_strcpy(child->name, parent->name, sizeof(child->name));
  child->state = PROCESS_STATE_READY;
  child->pml4 = (uint64_t *)child_dir;
  child->cr3 = child_dir->pml4_phys;
  child->heap_start = parent->heap_start;
  child->heap_brk = parent->heap_brk;
  child->tls_image_address = parent->tls_image_address;
  child->tls_file_size = parent->tls_file_size;
  child->tls_mem_size = parent->tls_mem_size;
  child->tls_alignment = parent->tls_alignment;
  process_inherit_cwd(child, parent);
  file_table_init(child->fd_table, parent->fd_table);
  if (shm_clone_process_mappings(parent, child) != 0 ||
      vm_clone_areas(parent, child) != 0) {
    shm_cleanup_process(child);
    vm_cleanup(child);
    file_table_destroy(child->fd_table);
    kfree(kernel_stack);
    vmm_free_directory(child_dir);
    kfree(child);
    return -1;
  }
  wait_queue_init(&child->wait_queue);

  thread_t *thread = thread_create_user_from_frame(
      child, child->name, frame, kernel_stack, KERNEL_STACK_SIZE);
  if (thread == NULL) {
    vm_cleanup(child);
    shm_cleanup_process(child);
    file_table_destroy(child->fd_table);
    kfree(kernel_stack);
    vmm_free_directory(child_dir);
    kfree(child);
    return -1;
  }

  child->main_thread = thread;
  thread->errno_address = thread_current()->errno_address;
  child->thread_list = thread;
  child->thread_count = 1;
  process_link(parent, child);

  KLOG_INFO_DEC("PROC", "Forked child PID: ", child->pid);
  return (int)child->pid;
}

typedef struct exec_strings {
  char *argv[EXEC_VECTOR_MAX + 1];
  char *envp[EXEC_VECTOR_MAX + 1];
  char filename[PROCESS_CWD_MAX];
  char strings[EXEC_STRING_BYTES];
} exec_strings_t;

static int exec_copy_vector(char **dest, char *const source[], char *storage,
                             size_t *used, int *count) {
  *count = 0;
  if (!source) { dest[0] = NULL; return 0; }
  for (int i = 0; i <= EXEC_VECTOR_MAX; ++i) {
    uintptr_t address = (uintptr_t)source;
    size_t offset = (size_t)i * sizeof(char *);
    char *text;
    if (address > UINT64_MAX - offset ||
        copy_from_user(&text, (void *)(address + offset), sizeof(text))) return -1;
    if (!text) { dest[i] = NULL; *count = i; return 0; }
    if (i == EXEC_VECTOR_MAX || *used == EXEC_STRING_BYTES ||
        copy_string_from_user(storage + *used, text, EXEC_STRING_BYTES - *used))
      return -1;
    dest[i] = storage + *used;
    *used += strlen(dest[i]) + 1;
  }
  return -1;
}

int process_execve(interrupt_frame_t *frame, const char *filename,
                   char *const argv[], char *const envp[]) {
  process_t *proc = process_current();
  thread_t *thread = thread_current();
  if (proc == NULL || proc == idle_process || thread == NULL || frame == NULL ||
      filename == NULL || proc->thread_count != 1) {
    return -1;
  }
  exec_strings_t *copy = kmalloc(sizeof(*copy));
  if (!copy) { KLOG_ERROR("EXEC", "Cannot allocate exec input snapshot"); return -1; }
  size_t used = 0;
  int argc, envc;
  if (copy_string_from_user(copy->filename, filename, sizeof(copy->filename)) ||
      exec_copy_vector(copy->argv, argv, copy->strings, &used, &argc) ||
      exec_copy_vector(copy->envp, envp, copy->strings, &used, &envc)) {
    KLOG_ERROR("EXEC", "Invalid or excessive exec argv/envp");
    kfree(copy);
    return -1;
  }
  if (!argc) {
    copy->argv[0] = copy->filename;
    copy->argv[1] = NULL;
    argc = 1;
  }
  process_t *image = process_spawn_environment(copy->filename, argc, copy->argv,
                                               envc, copy->envp);
  kfree(copy);
  if (image == NULL || image->main_thread == NULL || image->pml4 == NULL) {
    return -1;
  }

  thread_t *image_thread = image->main_thread;
  interrupt_frame_t new_frame =
      *(interrupt_frame_t *)(uintptr_t)image_thread->rsp;
  page_directory_t *old_dir = (page_directory_t *)proc->pml4;
  page_directory_t *new_dir = (page_directory_t *)image->pml4;

  scheduler_dequeue(image_thread);
  uint64_t flags = process_lock();
  process_unlink(image);
  process_unlock(flags);

  file_table_destroy(image->fd_table);
  vm_cleanup(proc);
  safe_strcpy(proc->name, image->name, sizeof(proc->name));
  safe_strcpy(thread->name, image->name, sizeof(thread->name));
  proc->pml4 = image->pml4;
  proc->cr3 = image->cr3;
  proc->heap_start = image->heap_start;
  proc->heap_brk = image->heap_brk;
  proc->tls_image_address = image->tls_image_address;
  proc->tls_file_size = image->tls_file_size;
  proc->tls_mem_size = image->tls_mem_size;
  proc->tls_alignment = image->tls_alignment;
  proc->vm_areas = image->vm_areas;
  thread->fs_base = image_thread->fs_base;
  thread->tls_mapping = image_thread->tls_mapping;
  thread->tls_mapping_size = image_thread->tls_mapping_size;
  thread->errno_address = 0;

  image->pml4 = NULL;
  image_thread->owner = NULL;
  kfree(image_thread->stack_base);
  image_thread->stack_base = NULL;
  xstate_destroy(image_thread->xstate);
  thread_completion_cleanup(image);
  kfree(image_thread);
  kfree(image);

  if (vmm_switch_directory(new_dir) != 0) {
    KLOG_ERROR("EXEC", "Failed to activate replacement address space");
    return -1;
  }
  file_table_close_on_exec(proc->fd_table);
  shm_cleanup_process(proc);
  vmm_free_directory(old_dir);
  xstate_reset(thread->xstate);
  xstate_restore(thread->xstate);
  wrmsr(0xC0000100, thread->fs_base);

  memcpy(frame, &new_frame, sizeof(*frame));
  frame->rax = 0;
  return 0;
}

/**
 * Lance un programme ELF et attend sa fin (bloquant pour le shell / thread appelant).
 *
 * @param filename  Chemin du fichier ELF à exécuter
 * @param argc      Nombre d'arguments
 * @param argv      Tableau d'arguments
 * @return          Exit status du processus, ou -1 si erreur de lancement
 */
int process_exec_and_wait(const char *filename, int argc, char **argv) {
  process_t *proc = process_spawn(filename, argc, argv);
  if (!proc) {
    return -1;
  }

  /* Attente bloquante via process_join */
  return process_join(proc);
}

/* ========================================
 * Nouvelles fonctions Multithreading
 * ======================================== */

process_t *process_create_kernel(const char *name, thread_entry_t entry,
                                 void *arg, uint32_t stack_size) {
  if (!multitasking_enabled || !entry) {
    return NULL;
  }

  KLOG_INFO("PROC", "Creating kernel process:");
  KLOG_INFO("PROC", name ? name : "<unnamed>");

  /* Allouer la structure du processus */
  process_t *proc = (process_t *)kmalloc(sizeof(process_t));
  if (!proc) {
    KLOG_ERROR("PROC", "Failed to allocate process structure");
    return NULL;
  }

  /* Initialiser */
  memset(proc, 0, sizeof(*proc));
  proc->pid = next_pid++;
  safe_strcpy(proc->name, name ? name : "", sizeof(proc->name));
  proc->state = PROCESS_STATE_READY;
  proc->should_terminate = 0;
  proc->exit_status = 0;
  proc->native_exit_reason = ALOS_PROCESS_EXIT_NORMAL;
  proc->uses_framebuffer = false;
  process_inherit_cwd(proc, current_process);
  file_table_init(proc->fd_table, current_process ? current_process->fd_table : NULL);

  proc->pml4 = (uint64_t *)vmm_get_kernel_directory();
  proc->cr3 = (uint64_t)proc->pml4;

  proc->stack_base = NULL;
  proc->stack_size = 0;
  proc->rsp = 0;
  proc->rsp0 = 0;

  proc->thread_count = 0;
  proc->thread_list = NULL;

  wait_queue_init(&proc->wait_queue);

  proc->parent = current_process;
  proc->first_child = NULL;
  proc->sibling_next = NULL;
  proc->sibling_prev = NULL;

  /* Créer le thread principal */
  thread_t *main_thread = thread_create_in_process(
      proc, name, entry, arg, stack_size, THREAD_PRIORITY_NORMAL);
  if (!main_thread) {
    KLOG_ERROR("PROC", "Failed to create main thread");
    file_table_destroy(proc->fd_table);
    kfree(proc);
    return NULL;
  }

  proc->main_thread = main_thread;
  proc->thread_list = main_thread;
  proc->thread_count = 1;

  /* Ajouter à la liste des processus */
  asm volatile("cli");

  proc->next = current_process->next;
  proc->prev = current_process;
  current_process->next->prev = proc;
  current_process->next = proc;

  /* Ajouter comme enfant du processus courant */
  if (current_process) {
    proc->sibling_next = current_process->first_child;
    if (current_process->first_child) {
      current_process->first_child->sibling_prev = proc;
    }
    current_process->first_child = proc;
  }

  asm volatile("sti");

  KLOG_INFO_DEC("PROC", "Created process PID: ", proc->pid);

  return proc;
}

void process_sleep_ms(uint32_t ms) { thread_sleep_ms(ms); }

void process_yield(void) { thread_yield(); }

static bool process_waiting_still_running(void *context) {
  process_t *proc = (process_t *)context;
  return proc->state == PROCESS_STATE_ZOMBIE ||
         proc->state == PROCESS_STATE_TERMINATED;
}

int process_join(process_t *proc) {
  if (!proc)
    return -1;

  uint32_t pid = proc->pid;
  proc_log_wait_begin(pid);

  /* Si le process tourne encore : bloquer sur sa wait_queue */
  if (proc->state != PROCESS_STATE_ZOMBIE &&
      proc->state != PROCESS_STATE_TERMINATED) {
    wait_queue_wait(&proc->wait_queue, process_waiting_still_running, proc);
  }

  /* Lire et sauvegarder exit_status avant le reap */
  int status = proc->exit_status;
  proc_log_wait_completed(pid, status);

  /* Effectuer le final reap (détachement et kfree) */
  process_reap(proc);

  return status;
}

typedef struct {
  process_t *parent;
  int pid;
} process_wait_context_t;

static process_t *process_find_waitable_child(process_t *parent, int pid) {
  process_t *child = parent->first_child;
  while (child != NULL) {
    if ((pid == -1 || child->pid == (uint32_t)pid) &&
        (child->state == PROCESS_STATE_ZOMBIE ||
         child->state == PROCESS_STATE_TERMINATED)) {
      return child;
    }
    child = child->sibling_next;
  }
  return NULL;
}

static bool process_child_waitable(void *context) {
  process_wait_context_t *wait = (process_wait_context_t *)context;
  return process_find_waitable_child(wait->parent, wait->pid) != NULL;
}

int process_waitpid(int pid, int *status, int options) {
  process_t *parent = process_current();
  if (parent == NULL || (pid == 0 || pid < -1) ||
      (options != 0 && options != 1)) {
    return -1;
  }

  process_t *child = parent->first_child;
  while (child != NULL && (pid != -1 && child->pid != (uint32_t)pid)) {
    child = child->sibling_next;
  }
  if (child == NULL) {
    return -1;
  }

  if (options == 1 &&
      process_find_waitable_child(parent, pid) == NULL) {
    return 0;
  }

  process_wait_context_t wait = {.parent = parent, .pid = pid};
  if (options == 0) {
    wait_queue_wait(&parent->wait_queue, process_child_waitable, &wait);
  }

  child = process_find_waitable_child(parent, pid);
  if (child == NULL) {
    return -1;
  }

  int child_pid = (int)child->pid;
  if (status != NULL) {
    *status = child->exit_status;
  }
  process_reap(child);
  return child_pid;
}

bool process_complete_exit(process_t *proc) {
  if (proc == NULL || proc == idle_process) {
    return false;
  }

  uint64_t flags = process_lock();
  process_t *child = proc->first_child;
  proc->first_child = NULL;
  while (child != NULL) {
    process_t *next = child->sibling_next;
    child->parent = idle_process;
    child->sibling_prev = NULL;
    child->sibling_next = idle_process->first_child;
    if (idle_process->first_child != NULL) {
      idle_process->first_child->sibling_prev = child;
    }
    idle_process->first_child = child;
    child = next;
  }
  process_t *parent = proc->parent;
  bool orphan = parent == NULL || parent == idle_process;
  process_unlock(flags);

  wait_queue_wake_all(&proc->wait_queue);
  if (parent != NULL) {
    wait_queue_wake_all(&parent->wait_queue);
  }
  return orphan;
}

void process_reap(process_t *proc) {
  if (proc == NULL || proc == idle_process) {
    return;
  }

  uint32_t pid = proc->pid;
  bool was_gui = proc->uses_framebuffer;
  proc_log_final_reap(pid);
  uint64_t flags = process_lock();
  process_unlink(proc);
  process_unlock(flags);
  kfree(proc);
  if (was_gui && !console_is_enabled()) {
    keyboard_clear_buffer();
    console_set_enabled(true);
    console_clear(VGA_COLOR_BLACK);
    console_refresh();
  }
}

static process_t *native_find_process_locked(int pid) {
    if (!process_list) return NULL;
    process_t *process = process_list;
    do {
      if (process->pid == (uint32_t)pid) return process;
      process = process->next;
    } while (process && process != process_list);
    return NULL;
  }

int process_native_query(int pid, alos_process_info_t *output) {
    if (pid <= 0) return -EINVAL;
    if (!user_range_valid(output, sizeof(*output), true)) return -EFAULT;
    process_t *caller = process_current();
    if (!caller) return -ESRCH;
    uint64_t flags = process_lock();
    process_t *target = native_find_process_locked(pid);
    int result = 0;
    alos_process_info_t information = {0};
    if (!target || target == idle_process) result = -ESRCH;
    else if (target != caller && target->parent != caller) result = -EPERM;
    else {
      information.pid = target->pid;
      information.parent_pid = target->parent ? target->parent->pid : 0;
      information.thread_count = target->thread_count;
      bool live_thread = false;
      for (thread_t *thread = target->thread_list; thread; thread = thread->proc_next)
        if (thread->state != THREAD_STATE_ZOMBIE) live_thread = true;
      information.state = target->state == PROCESS_STATE_ZOMBIE
                            ? ALOS_PROCESS_ZOMBIE
                            : (target->should_terminate || !live_thread)
                                ? ALOS_PROCESS_EXITING : ALOS_PROCESS_ALIVE;
      if (target->main_thread) {
        information.main_thread_nice = thread_get_nice(target->main_thread);
        information.has_main_thread_nice = 1;
      }
    }
    process_unlock(flags);
    if (!result && copy_to_user(output, &information, sizeof(information)))
      result = -EFAULT;
    return result;
  }

int process_native_terminate(int pid, int raw_status) {
    if (pid <= 0) return -EINVAL;
    process_t *caller = process_current();
    if (!caller) return -ESRCH;
    uint64_t flags = process_lock();
    process_t *target = native_find_process_locked(pid);
    int result = 0;
    if (!target || target == idle_process) result = -ESRCH;
    else if (target == caller) result = -EINVAL; /* Utiliser EXIT_GROUP pour soi. */
    else if (target->parent != caller) result = -EPERM;
    else if (target->state != PROCESS_STATE_ZOMBIE && !target->should_terminate) {
      bool live_thread = false;
      for (thread_t *thread = target->thread_list; thread; thread = thread->proc_next)
        if (thread->state != THREAD_STATE_ZOMBIE) live_thread = true;
      if (!live_thread) {
        process_unlock(flags);
        return 0;
      }
      target->native_exit_reason = ALOS_PROCESS_EXIT_FORCED;
      target->exit_status = raw_status;
      target->should_terminate = 1;
      for (thread_t *thread = target->thread_list; thread; thread = thread->proc_next)
        if (thread->state != THREAD_STATE_ZOMBIE) thread_kill(thread, raw_status);
    }
    process_unlock(flags);
    return result;
  }

  typedef struct {
    process_t *parent;
    int pid;
  } native_wait_context_t;

static bool native_child_ready_or_gone(void *opaque) {
    native_wait_context_t *context = opaque;
    bool found = false;
    for (process_t *child = context->parent->first_child; child;
         child = child->sibling_next) {
      if (context->pid == -1 || child->pid == (uint32_t)context->pid) {
        found = true;
        if (child->state == PROCESS_STATE_ZOMBIE && child->thread_count == 0)
          return true;
      }
    }
    return !found;
  }

int process_native_wait(int pid, alos_process_exit_t *output, uint32_t timeout_ms) {
    if (pid == 0 || pid < -1) return -EINVAL;
    if (!user_range_valid(output, sizeof(*output), true)) return -EFAULT;
    process_t *parent = process_current();
    if (!parent) return -ESRCH;
    native_wait_context_t context = {parent, pid};
    if (timeout_ms && !native_child_ready_or_gone(&context)) {
      wait_queue_wait_timeout(&parent->wait_queue, native_child_ready_or_gone,
                               &context, timeout_ms == ALOS_PROCESS_WAIT_FOREVER
                                           ? 0 : timeout_ms);
    }
    uint64_t flags = process_lock();
    bool found = false;
    process_t *ready = NULL;
    for (process_t *child = parent->first_child; child; child = child->sibling_next) {
      if (pid == -1 || child->pid == (uint32_t)pid) {
        found = true;
        if (child->state == PROCESS_STATE_ZOMBIE && child->thread_count == 0) {
          ready = child;
          break;
        }
      }
    }
    if (!ready) {
      process_unlock(flags);
      return found ? 0 : -ECHILD;
    }
    if (ready->pid > INT32_MAX) {
      process_unlock(flags);
      return -EOVERFLOW;
    }
    alos_process_exit_t exit = {ready->pid, ready->exit_status,
                                ready->native_exit_reason, 0};
    if (copy_to_user(output, &exit, sizeof(exit))) {
      process_unlock(flags);
      return -EFAULT;
    }
    /* Consommer l'enfant sous le lock avant de le liberer : deux waiters ne
     * doivent pas reap le meme processus. Ses ressources ont deja ete nettoyees. */
    bool was_gui = ready->uses_framebuffer;
    process_unlink(ready);
    process_unlock(flags);
    proc_log_final_reap(exit.pid);
    kfree(ready);
    if (was_gui && !console_is_enabled()) {
      keyboard_clear_buffer();
      console_set_enabled(true);
      console_clear(VGA_COLOR_BLACK);
      console_refresh();
    }
    wait_queue_wake_all(&parent->wait_queue);
    return (int)exit.pid;
  }

void process_kill(process_t *proc) {
  if (!proc || proc == idle_process)
    return;

  asm volatile("cli");

  proc->should_terminate = 1;
  proc->state = PROCESS_STATE_TERMINATED;

  /* Tuer tous les threads */
  thread_t *thread = proc->thread_list;
  while (thread) {
    thread_kill(thread, -1);
    thread = thread->proc_next;
  }

  /* Réveiller tous les processus en attente */
  wait_queue_wake_all(&proc->wait_queue);

  asm volatile("sti");
}

void process_kill_tree(process_t *proc) {
  if (!proc)
    return;

  /* Tuer tous les enfants récursivement */
  process_t *child = proc->first_child;
  while (child) {
    process_t *next = child->sibling_next;
    process_kill_tree(child);
    child = next;
  }

  /* Tuer le processus lui-même */
  process_kill(proc);
}

process_t *process_current(void) { return current_process; }

bool process_is_zombie(process_t *proc) {
  return proc && proc->state == PROCESS_STATE_ZOMBIE;
}

const char *process_state_name(process_state_t state) {
  switch (state) {
  case PROCESS_STATE_READY:
    return "READY";
  case PROCESS_STATE_RUNNING:
    return "RUNNING";
  case PROCESS_STATE_BLOCKED:
    return "BLOCKED";
  case PROCESS_STATE_ZOMBIE:
    return "ZOMBIE";
  case PROCESS_STATE_TERMINATED:
    return "TERMINATED";
  default:
    return "UNKNOWN";
  }
}

size_t process_snapshot(process_info_t *buffer, size_t capacity) {
  if (!buffer || capacity == 0)
    return 0;

  size_t count = 0;

  asm volatile("cli");

  process_t *proc = process_list;
  if (proc) {
    do {
      if (count >= capacity)
        break;

      buffer[count].pid = proc->pid;
      buffer[count].state = proc->state;
      buffer[count].name = proc->name;
      buffer[count].is_current = (proc == current_process);
      buffer[count].time_slice_remaining = 0;

      /* Récupérer les infos du thread principal */
      if (proc->main_thread) {
        buffer[count].thread_state = proc->main_thread->state;
        buffer[count].thread_name = proc->main_thread->name;
        buffer[count].time_slice_remaining =
            proc->main_thread->time_slice_remaining;
      } else {
        buffer[count].thread_state = THREAD_STATE_READY;
        buffer[count].thread_name = "";
      }

      count++;
      proc = proc->next;
    } while (proc != process_list);
  }

  asm volatile("sti");

  return count;
}

static void terminate_fault(uint64_t int_no, uint64_t rip, uint64_t fault_addr,
                             uint64_t error_code, bool backing_error)
    __attribute__((noreturn));

static void terminate_fault(uint64_t int_no, uint64_t rip, uint64_t fault_addr,
                             uint64_t error_code, bool backing_error) {
  /* Désactiver les interruptions pour manipulation atomique des états */
  asm volatile("cli");

  /* Récupérer le thread courant et son processus propriétaire */
  thread_t *curr = thread_current();
  process_t *proc = curr ? curr->owner : NULL;

  /* Mapper le numéro d'exception sur un nom clair et un signal */
  const char *fault_name = "Fatal Exception";
  const char *sig_name = "SIGSEGV";
  int sig = 11;

  switch (int_no) {
  case 0:
    fault_name = "Division By Zero";
    sig_name = "SIGFPE";
    sig = 8;
    break;
  case 1:
    fault_name = "Debug Exception";
    sig_name = "SIGTRAP";
    sig = 5;
    break;
  case 3:
    fault_name = "Breakpoint";
    sig_name = "SIGTRAP";
    sig = 5;
    break;
  case 4:
    fault_name = "Overflow";
    sig_name = "SIGFPE";
    sig = 8;
    break;
  case 5:
    fault_name = "Bound Range Exceeded";
    sig_name = "SIGSEGV";
    sig = 11;
    break;
  case 6:
    fault_name = "Invalid Opcode (Illegal Instruction)";
    sig_name = "SIGILL";
    sig = 4;
    break;
  case 7:
    fault_name = "Device Not Available";
    sig_name = "SIGFPE";
    sig = 8;
    break;
  case 8:
    fault_name = "Double Fault";
    sig_name = "SIGSEGV";
    sig = 11;
    break;
  case 10:
    fault_name = "Invalid TSS";
    sig_name = "SIGSEGV";
    sig = 11;
    break;
  case 11:
    fault_name = "Segment Not Present";
    sig_name = "SIGBUS";
    sig = 7;
    break;
  case 12:
    fault_name = "Stack-Segment Fault";
    sig_name = "SIGBUS";
    sig = 7;
    break;
  case 13:
    fault_name = "General Protection Fault";
    sig_name = "SIGSEGV";
    sig = 11;
    break;
  case 14:
    fault_name = "Page Fault (Segmentation Fault)";
    sig_name = "SIGSEGV";
    sig = 11;
    break;
  case 16:
    fault_name = "x87 FPU Floating-Point Error";
    sig_name = "SIGFPE";
    sig = 8;
    break;
  case 17:
    fault_name = "Alignment Check";
    sig_name = "SIGBUS";
    sig = 7;
    break;
  case 19:
    fault_name = "SIMD Floating-Point Exception";
    sig_name = "SIGFPE";
    sig = 8;
    break;
  default:
    break;
  }

  if (backing_error) {
    fault_name = "Page Fault (mmap backing unavailable)";
    sig_name = "SIGBUS";
    sig = 7;
  }

  /* Log détaillé sur le port série */
  KLOG_ERROR("USER_FAULT", "========================================");
  KLOG_ERROR("USER_FAULT", "   FATAL USER PROCESS FAULT DETECTED    ");
  KLOG_ERROR("USER_FAULT", "========================================");
  if (proc) {
    KLOG_ERROR("USER_FAULT", proc->name);
    KLOG_ERROR_DEC("USER_FAULT", "PID: ", proc->pid);
  }
  KLOG_ERROR("USER_FAULT", fault_name);
  KLOG_ERROR_HEX("USER_FAULT", "INT: ", (uint32_t)int_no);
  KLOG_ERROR_HEX("USER_FAULT", "Error code: ", (uint32_t)error_code);
  KLOG_ERROR_HEX("USER_FAULT", "RIP (high): ", (uint32_t)(rip >> 32));
  KLOG_ERROR_HEX("USER_FAULT", "RIP (low): ", (uint32_t)rip);
  if (int_no == 14) {
    KLOG_ERROR_HEX("USER_FAULT", "Fault Addr (high): ", (uint32_t)(fault_addr >> 32));
    KLOG_ERROR_HEX("USER_FAULT", "Fault Addr (low): ", (uint32_t)fault_addr);
  }

  /* Si le processus utilisait la GUI ou si la console est désactivée, restaurer la console */
  bool was_gui = false;
  if (proc && proc->uses_framebuffer) {
    was_gui = true;
  }
  if (!console_is_enabled() || was_gui) {
    keyboard_clear_buffer();
    input_clear_events();
    console_set_enabled(true);
    console_clear(VGA_COLOR_BLACK);
    console_refresh();
  }

  /* Notification visuelle sur la console texte */
  console_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
  console_puts("\n[KERNEL] Process '");
  console_puts(proc ? proc->name : "<unknown>");
  console_puts("' (PID ");
  if (proc) {
    console_put_dec(proc->pid);
  } else {
    console_putc('?');
  }
  console_puts(") terminated: ");
  console_puts(fault_name);
  console_puts(" (");
  console_puts(sig_name);
  console_puts(")\n");
  console_puts("         RIP: 0x");
  console_put_hex64(rip);
  if (int_no == 14) {
    console_puts(" | Fault Addr: 0x");
    console_put_hex64(fault_addr);
  }
  console_puts(" | Error code: 0x");
  console_put_hex((uint32_t)error_code);
  console_puts("\n");
  console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

  int exit_status = 128 + sig;
  if (proc) {
    proc->exit_status = exit_status;
    /* Tuer les autres threads du même processus pour éviter des zombies orphelins */
    thread_t *t = proc->thread_list;
    while (t) {
      if (t != curr && t->state != THREAD_STATE_ZOMBIE) {
        thread_kill(t, exit_status);
      }
      t = t->proc_next;
    }
  }

  /* Terminer le thread courant - ne retourne JAMAIS */
  thread_exit(exit_status);

  for (;;) {
    asm volatile("hlt");
  }
}

void process_terminate_fault(uint64_t int_no, uint64_t rip, uint64_t fault_addr,
                             uint64_t error_code) {
  terminate_fault(int_no, rip, fault_addr, error_code, false);
}

void process_terminate_mapping_fault(uint64_t rip, uint64_t fault_addr,
                                     uint64_t error_code) {
  terminate_fault(14, rip, fault_addr, error_code, true);
}

void process_log_fault_context(const interrupt_frame_t *frame, uint64_t address) {
  KLOG_ERROR_DEC("USER_FAULT", "TID: ", thread_get_tid());
  KLOG_ERROR_HEX64("USER_FAULT", "RSP: ", frame->rsp);
  KLOG_ERROR_HEX64("USER_FAULT", "RAX: ", frame->rax);
  KLOG_ERROR_HEX64("USER_FAULT", "RFLAGS: ", frame->rflags);
  process_t *process = process_current();
  if (process && process->pml4) {
    KLOG_ERROR_HEX64("USER_FAULT", "Fault page entry: ",
        vmm_get_page_entry((page_directory_t *)process->pml4, address));
  }
}
