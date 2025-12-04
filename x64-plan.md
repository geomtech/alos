/* ============================================
 * CORRECTIF COMPLET POUR LE BUG CONDVAR_WAIT
 * ============================================
 * 
 * Problème : condvar_wait appelé depuis sys_accept corrompt le contexte
 * Solution : Utiliser un flag pour indiquer qu'un yield est nécessaire
 */

/* ========== PARTIE 1: sync.c ========== */

/* Ajouter un flag dans thread_t pour indiquer qu'un yield est nécessaire */
// Dans thread.h, ajouter :
// bool needs_yield;  /* True if thread should yield after current syscall */

void condvar_wait(condvar_t *cv, mutex_t *mutex)
{
    if (!cv || !mutex) return;
    
    thread_t *current = thread_current();
    if (!current) return;
    
    uint64_t flags = cpu_save_flags();
    cpu_cli();
    
    spinlock_lock(&cv->lock);
    
    /* Add to wait queue BEFORE releasing mutex */
    current->state = THREAD_STATE_BLOCKED;
    current->waiting_queue = &cv->waiters;
    
    thread_t **tail = &cv->waiters.head;
    while (*tail) {
        tail = &((*tail)->wait_queue_next);
    }
    *tail = current;
    current->wait_queue_next = NULL;
    if (!cv->waiters.tail) {
        cv->waiters.tail = current;
    }
    
    spinlock_unlock(&cv->lock);
    
    /* Release mutex */
    mutex_unlock(mutex);
    
    /* SOLUTION: Marquer qu'on doit céder le CPU, puis boucler.
     * Le syscall dispatcher vérifiera ce flag et fera le yield proprement. */
    current->needs_yield = true;
    
    /* Wait until signaled - keep checking in a loop.
     * Each iteration checks if we were woken up. */
    while (current->state == THREAD_STATE_BLOCKED) {
        /* Si on est en kernel thread, yield directement */
        if (current->owner == NULL) {
            scheduler_schedule();
        } else {
            /* Thread user : juste une pause courte.
             * Le syscall dispatcher fera le vrai yield. */
            __asm__ volatile("pause");
            
            /* Si on a été réveillé (signaled), sortir */
            if (current->state != THREAD_STATE_BLOCKED) {
                break;
            }
            
            /* Sinon, yield via syscall dispatcher */
            /* IMPORTANT: On ne peut PAS appeler scheduler_schedule ici
             * car on est au milieu d'un syscall avec un frame incomplet ! */
            
            /* À la place, on retourne au syscall qui va vérifier needs_yield */
            break;
        }
    }
    
    /* Re-acquire mutex */
    mutex_lock(mutex);
    
    current->waiting_queue = NULL;
    current->needs_yield = false;  /* Reset flag */
    
    cpu_restore_flags(flags);
}


/* ========== PARTIE 2: syscall.c ========== */

void syscall_dispatcher(syscall_regs_t* regs)
{
    uint32_t syscall_num = regs->rax;
    int result = -1;
    
    /* ... traitement normal des syscalls ... */
    
    switch (syscall_num) {
        case SYS_ACCEPT:
            result = sys_accept((int)regs->rdi, (sockaddr_in_t*)regs->rsi, (int*)regs->rdx);
            break;
        
        /* ... autres syscalls ... */
    }
    
    /* Retourner le résultat dans RAX */
    regs->rax = (uint32_t)result;
    
    /* NOUVEAU: Vérifier si le thread doit céder le CPU */
    thread_t *current = thread_current();
    if (current && current->needs_yield && current->state == THREAD_STATE_BLOCKED) {
        /* Le thread est bloqué (ex: dans condvar_wait).
         * On doit céder le CPU MAINTENANT, avec le contexte syscall complet.
         * 
         * IMPORTANT: Ne PAS retourner au user space - appeler scheduler.
         */
        KLOG_INFO("SYSCALL", "Thread blocked, yielding from syscall");
        
        /* Sauvegarder le contexte complet (RIP, RSP, etc. sont dans regs) */
        current->rsp = (uint64_t)regs;  /* Sauvegarder le frame syscall */
        
        /* Marquer comme BLOCKED (déjà fait par condvar_wait) */
        /* current->state = THREAD_STATE_BLOCKED; */
        
        /* Retirer de la run queue */
        scheduler_dequeue(current);
        
        /* Appeler le scheduler - il va choisir un autre thread */
        scheduler_schedule();
        
        /* Quand on revient ici, on a été réveillé.
         * Le contexte est restauré, on peut continuer. */
        KLOG_INFO("SYSCALL", "Thread woken up, resuming syscall");
    }
    
    KLOG_INFO("SYSCALL", "syscall_dispatcher returning");
}


/* ========== PARTIE 3: thread.h ========== */

/* Ajouter dans la structure thread_t : */

typedef struct thread {
    /* ... champs existants ... */
    
    /* Yield flag for blocking syscalls */
    bool needs_yield;           /* True if thread should yield after syscall */
    
    /* ... reste des champs ... */
} thread_t;


/* ========== PARTIE 4: thread.c ========== */

/* Dans thread_create() et thread_create_user(), initialiser : */
thread->needs_yield = false;



/* ============================================
 * SOLUTION PROPRE : SYSCALLS BLOQUANTS
 * ============================================
 * 
 * Principe : Quand un syscall doit bloquer (accept, read, etc.),
 * on sauvegarde le contexte complet et on yield proprement.
 * Quand le thread est réveillé, on reprend au point de sauvegarde.
 */

/* ========== AJOUT dans syscall.h ========== */

/* État d'un syscall bloquant */
typedef enum {
    SYSCALL_STATE_RUNNING,      /* Syscall en cours */
    SYSCALL_STATE_BLOCKED,      /* Syscall bloqué, attente événement */
    SYSCALL_STATE_COMPLETED,    /* Syscall terminé, résultat disponible */
} syscall_state_t;

/* Context de reprise pour syscalls bloquants */
typedef struct {
    syscall_state_t state;      /* État du syscall */
    uint32_t syscall_num;       /* Numéro du syscall */
    int result;                 /* Résultat (quand completed) */
    
    /* Arguments sauvegardés pour reprise */
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    
    /* Contexte spécifique au syscall (union) */
    union {
        struct {
            int listen_fd;
            uint16_t port;
        } accept_ctx;
        
        struct {
            int fd;
            uint8_t* buf;
            int len;
        } recv_ctx;
    };
} syscall_context_t;


/* ========== AJOUT dans thread.h ========== */

typedef struct thread {
    /* ... champs existants ... */
    
    /* Syscall context for blocking syscalls */
    syscall_context_t* syscall_ctx;  /* NULL if not in syscall */
    
    /* ... reste ... */
} thread_t;


/* ========== MODIFICATION de sys_accept dans syscall.c ========== */

static int sys_accept(int fd, sockaddr_in_t* addr, int* len)
{
    (void)len;
    
    thread_t* current = thread_current();
    if (!current) return -1;
    
    /* Si c'est la première fois qu'on entre dans ce syscall */
    if (current->syscall_ctx == NULL || 
        current->syscall_ctx->state == SYSCALL_STATE_RUNNING) {
        
        /* Vérifier le FD */
        if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FILE_TYPE_SOCKET) {
            return -1;
        }
        
        tcp_socket_t* listen_sock = fd_table[fd].socket;
        if (listen_sock == NULL || listen_sock->state != TCP_STATE_LISTEN) {
            return -1;
        }
        
        uint16_t port = listen_sock->local_port;
        
        /* Chercher un client prêt SANS bloquer */
        tcp_socket_t* client_sock = tcp_find_ready_client(port);
        
        if (client_sock != NULL) {
            /* Connexion immédiatement disponible - path rapide */
            goto accept_ready;
        }
        
        /* Pas de connexion prête - on doit bloquer */
        KLOG_DEBUG("SYSCALL", "sys_accept: no client ready, blocking...");
        
        /* Allouer/initialiser le contexte syscall */
        if (current->syscall_ctx == NULL) {
            current->syscall_ctx = (syscall_context_t*)kmalloc(sizeof(syscall_context_t));
            if (!current->syscall_ctx) return -1;
        }
        
        current->syscall_ctx->state = SYSCALL_STATE_BLOCKED;
        current->syscall_ctx->syscall_num = SYS_ACCEPT;
        current->syscall_ctx->accept_ctx.listen_fd = fd;
        current->syscall_ctx->accept_ctx.port = port;
        
        /* Bloquer le thread en attendant une connexion */
        mutex_lock(&listen_sock->accept_mutex);
        
        while (tcp_find_ready_client(port) == NULL) {
            /* Attendre sur la condition variable.
             * IMPORTANT : condvar_wait va appeler scheduler_schedule(),
             * mais cette fois on est préparés - le contexte est sauvegardé. */
            condvar_wait(&listen_sock->accept_cv, &listen_sock->accept_mutex);
        }
        
        mutex_unlock(&listen_sock->accept_mutex);
        
        /* On a été réveillé - une connexion est prête ! */
        KLOG_DEBUG("SYSCALL", "sys_accept: woken up, connection available");
    }
    
    /* À ce point, soit :
     * 1. C'est la première fois et une connexion est prête (path rapide)
     * 2. On vient d'être réveillé après un blocage
     */
    
accept_ready:
    {
        /* Récupérer les paramètres (soit des arguments, soit du ctx) */
        uint16_t port;
        if (current->syscall_ctx && 
            current->syscall_ctx->state == SYSCALL_STATE_BLOCKED) {
            port = current->syscall_ctx->accept_ctx.port;
        } else {
            tcp_socket_t* listen_sock = fd_table[fd].socket;
            port = listen_sock->local_port;
        }
        
        /* Récupérer le socket client */
        tcp_socket_t* client_sock = tcp_find_ready_client(port);
        if (!client_sock) {
            /* Ne devrait pas arriver */
            KLOG_ERROR("SYSCALL", "sys_accept: no client after wakeup!");
            return -1;
        }
        
        /* Allouer un FD pour le client */
        int client_fd = fd_alloc();
        if (client_fd < 0) {
            net_lock();
            tcp_close(client_sock);
            net_unlock();
            return -1;
        }
        
        /* Configurer le FD */
        fd_table[client_fd].type = FILE_TYPE_SOCKET;
        fd_table[client_fd].socket = client_sock;
        fd_table[client_fd].flags = O_RDWR;
        
        /* Remplir l'adresse du client */
        if (addr != NULL) {
            addr->sin_family = AF_INET;
            addr->sin_port = htons(client_sock->remote_port);
            addr->sin_addr = ((uint32_t)client_sock->remote_ip[0]) |
                             ((uint32_t)client_sock->remote_ip[1] << 8) |
                             ((uint32_t)client_sock->remote_ip[2] << 16) |
                             ((uint32_t)client_sock->remote_ip[3] << 24);
        }
        
        /* Nettoyer le contexte syscall */
        if (current->syscall_ctx) {
            kfree(current->syscall_ctx);
            current->syscall_ctx = NULL;
        }
        
        KLOG_DEBUG_DEC("SYSCALL", "sys_accept: success, fd=", client_fd);
        return client_fd;
    }
}


/* ========== MODIFICATION de syscall_dispatcher ========== */

void syscall_dispatcher(syscall_regs_t* regs)
{
    /* ... début normal ... */
    
    /* Vérifier si on reprend un syscall bloqué */
    thread_t* current = thread_current();
    if (current && current->syscall_ctx && 
        current->syscall_ctx->state == SYSCALL_STATE_BLOCKED) {
        
        /* On revient après un blocage - reprendre le syscall */
        KLOG_DEBUG("SYSCALL", "Resuming blocked syscall");
        
        /* Le syscall va vérifier son état et continuer */
        /* Les arguments sont dans syscall_ctx */
    }
    
    /* ... traitement normal des syscalls ... */
}