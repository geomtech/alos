/* src/kernel/atomic.h - Opérations atomiques pour synchronisation */
#ifndef ATOMIC_H
#define ATOMIC_H

#include <stdint.h>

/**
 * Type atomique 32-bit
 * Utiliser ce type pour les compteurs et flags partagés entre threads
 */
typedef struct {
    volatile int32_t value;
} atomic_t;

/**
 * Type atomique 32-bit non-signé
 */
typedef struct {
    volatile uint32_t value;
} atomic_u32_t;

/* ============================================ */
/*        Initialisation                        */
/* ============================================ */

#define ATOMIC_INIT(val) { .value = (val) }

static inline void atomic_set(atomic_t *v, int32_t val)
{
    v->value = val;
}

static inline void atomic_u32_set(atomic_u32_t *v, uint32_t val)
{
    v->value = val;
}

/* ============================================ */
/*        Lecture atomique                      */
/* ============================================ */

/**
 * Lecture atomique d'une valeur
 * Garantit une lecture cohérente même si un autre thread écrit
 */
static inline int32_t atomic_load(atomic_t *v)
{
    return __sync_fetch_and_add(&v->value, 0);
}

static inline uint32_t atomic_u32_load(atomic_u32_t *v)
{
    return __sync_fetch_and_add(&v->value, 0);
}

/* ============================================ */
/*        Écriture atomique                     */
/* ============================================ */

/**
 * Écriture atomique d'une valeur
 * Garantit une écriture cohérente
 */
static inline void atomic_store(atomic_t *v, int32_t val)
{
    __sync_lock_test_and_set(&v->value, val);
}

static inline void atomic_u32_store(atomic_u32_t *v, uint32_t val)
{
    __sync_lock_test_and_set(&v->value, val);
}

/* ============================================ */
/*        Incrémentation / Décrémentation       */
/* ============================================ */

/**
 * Incrémente atomiquement et retourne la NOUVELLE valeur
 */
static inline int32_t atomic_inc(atomic_t *v)
{
    return __sync_add_and_fetch(&v->value, 1);
}

static inline uint32_t atomic_u32_inc(atomic_u32_t *v)
{
    return __sync_add_and_fetch(&v->value, 1);
}

/**
 * Décrémente atomiquement et retourne la NOUVELLE valeur
 */
static inline int32_t atomic_dec(atomic_t *v)
{
    return __sync_sub_and_fetch(&v->value, 1);
}

static inline uint32_t atomic_u32_dec(atomic_u32_t *v)
{
    return __sync_sub_and_fetch(&v->value, 1);
}

/**
 * Incrémente atomiquement et retourne l'ANCIENNE valeur
 */
static inline int32_t atomic_fetch_inc(atomic_t *v)
{
    return __sync_fetch_and_add(&v->value, 1);
}

static inline uint32_t atomic_u32_fetch_inc(atomic_u32_t *v)
{
    return __sync_fetch_and_add(&v->value, 1);
}

/**
 * Décrémente atomiquement et retourne l'ANCIENNE valeur
 */
static inline int32_t atomic_fetch_dec(atomic_t *v)
{
    return __sync_fetch_and_sub(&v->value, 1);
}

static inline uint32_t atomic_u32_fetch_dec(atomic_u32_t *v)
{
    return __sync_fetch_and_sub(&v->value, 1);
}

/* ============================================ */
/*        Addition / Soustraction               */
/* ============================================ */

/**
 * Ajoute atomiquement et retourne la NOUVELLE valeur
 */
static inline int32_t atomic_add(atomic_t *v, int32_t val)
{
    return __sync_add_and_fetch(&v->value, val);
}

static inline uint32_t atomic_u32_add(atomic_u32_t *v, uint32_t val)
{
    return __sync_add_and_fetch(&v->value, val);
}

/**
 * Soustrait atomiquement et retourne la NOUVELLE valeur
 */
static inline int32_t atomic_sub(atomic_t *v, int32_t val)
{
    return __sync_sub_and_fetch(&v->value, val);
}

static inline uint32_t atomic_u32_sub(atomic_u32_t *v, uint32_t val)
{
    return __sync_sub_and_fetch(&v->value, val);
}

/**
 * Ajoute atomiquement et retourne l'ANCIENNE valeur
 */
static inline int32_t atomic_fetch_add(atomic_t *v, int32_t val)
{
    return __sync_fetch_and_add(&v->value, val);
}

static inline uint32_t atomic_u32_fetch_add(atomic_u32_t *v, uint32_t val)
{
    return __sync_fetch_and_add(&v->value, val);
}

/* ============================================ */
/*        Compare-And-Swap (CAS)                */
/* ============================================ */

/**
 * Compare-and-swap atomique
 * Si *v == expected, alors *v = desired
 * Retourne l'ancienne valeur (permet de vérifier si le swap a réussi)
 * 
 * Usage typique:
 *   int32_t old = atomic_cmpxchg(&counter, expected, new_val);
 *   if (old == expected) { // swap réussi }
 */
static inline int32_t atomic_cmpxchg(atomic_t *v, int32_t expected, int32_t desired)
{
    return __sync_val_compare_and_swap(&v->value, expected, desired);
}

static inline uint32_t atomic_u32_cmpxchg(atomic_u32_t *v, uint32_t expected, uint32_t desired)
{
    return __sync_val_compare_and_swap(&v->value, expected, desired);
}

/**
 * Compare-and-swap qui retourne true si le swap a réussi
 */
static inline int atomic_cmpxchg_bool(atomic_t *v, int32_t expected, int32_t desired)
{
    return __sync_bool_compare_and_swap(&v->value, expected, desired);
}

static inline int atomic_u32_cmpxchg_bool(atomic_u32_t *v, uint32_t expected, uint32_t desired)
{
    return __sync_bool_compare_and_swap(&v->value, expected, desired);
}

/* ============================================ */
/*        Opérations Bit-à-bit                  */
/* ============================================ */

/**
 * OR atomique, retourne l'ancienne valeur
 */
static inline int32_t atomic_fetch_or(atomic_t *v, int32_t val)
{
    return __sync_fetch_and_or(&v->value, val);
}

/**
 * AND atomique, retourne l'ancienne valeur
 */
static inline int32_t atomic_fetch_and(atomic_t *v, int32_t val)
{
    return __sync_fetch_and_and(&v->value, val);
}

/**
 * XOR atomique, retourne l'ancienne valeur
 */
static inline int32_t atomic_fetch_xor(atomic_t *v, int32_t val)
{
    return __sync_fetch_and_xor(&v->value, val);
}

/* ============================================ */
/*        Tests utilitaires                     */
/* ============================================ */

/**
 * Décrémente et retourne true si le résultat est zéro
 * Utile pour les compteurs de référence
 */
static inline int atomic_dec_and_test(atomic_t *v)
{
    return __sync_sub_and_fetch(&v->value, 1) == 0;
}

/**
 * Incrémente et retourne true si le résultat est zéro
 * (cas rare, mais peut arriver avec overflow)
 */
static inline int atomic_inc_and_test(atomic_t *v)
{
    return __sync_add_and_fetch(&v->value, 1) == 0;
}

/* ============================================ */
/*        Barrières mémoire                     */
/* ============================================ */

/**
 * Barrière mémoire complète
 * Force la complétion de toutes les opérations mémoire avant de continuer
 */
static inline void atomic_memory_barrier(void)
{
    __sync_synchronize();
}

/**
 * Barrière de lecture (acquérir)
 */
#define atomic_read_barrier() __asm__ __volatile__("" ::: "memory")

/**
 * Barrière d'écriture (relâcher)
 */
#define atomic_write_barrier() __asm__ __volatile__("" ::: "memory")

#endif /* ATOMIC_H */
