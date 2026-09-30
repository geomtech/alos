#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>

static char *empty_environment[] = {NULL};
char **environ = empty_environment;
static char **owned_vector;
static pthread_mutex_t environment_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct environment_string {
    struct environment_string *next;
    char text[];
} environment_string_t;
static environment_string_t *owned_strings;

static int lock_environment(void) {
    int error = pthread_mutex_lock(&environment_lock);
    if (error) { errno = error; return -1; }
    return 0;
}

static void unlock_environment(void) {
    int error = pthread_mutex_unlock(&environment_lock);
    if (error) {
        fprintf(stderr, "environment: mutex unlock failed: %d\n", error);
        abort();
    }
}

static size_t name_length(const char *name) {
    if (!name || !*name || strchr(name, '=')) return 0;
    return strlen(name);
}

static int matches(const char *entry, const char *name, size_t length) {
    return !strncmp(entry, name, length) && entry[length] == '=';
}

/* Seuls les textes crees par setenv sont liberes, jamais ceux du CRT/appelant. */
static void collect_strings(void) {
    environment_string_t **link = &owned_strings;
    while (*link) {
        environment_string_t *node = *link;
        int present = 0;
        if (environ) for (size_t i = 0; environ[i]; ++i)
            if (environ[i] == node->text) { present = 1; break; }
        if (present) link = &node->next;
        else { *link = node->next; free(node); }
    }
}

char *getenv(const char *name) {
    size_t length = name_length(name);
    if (!length) return NULL;
    int saved_errno = errno;
    if (lock_environment()) return NULL;
    char *result = NULL;
    if (environ) for (size_t i = 0; environ[i]; ++i)
        if (matches(environ[i], name, length)) {
            result = environ[i] + length + 1;
            break;
        }
    unlock_environment();
    errno = saved_errno;
    return result;
}

int setenv(const char *name, const char *value, int overwrite) {
    size_t length = name_length(name);
    if (!length || !value) { errno = EINVAL; return -1; }
    int saved_errno = errno;
    if (lock_environment()) return -1;
    size_t count = 0, found = SIZE_MAX;
    if (environ) for (; environ[count]; ++count)
        if (found == SIZE_MAX && matches(environ[count], name, length)) found = count;
    if (found != SIZE_MAX && !overwrite) {
        unlock_environment();
        errno = saved_errno;
        return 0;
    }
    size_t value_length = strlen(value);
    if (length > SIZE_MAX - sizeof(environment_string_t) - 2 ||
        value_length > SIZE_MAX - sizeof(environment_string_t) - 2 - length ||
        count > SIZE_MAX / sizeof(char *) - 2) {
        unlock_environment();
        errno = ENOMEM;
        return -1;
    }
    environment_string_t *node = malloc(sizeof(*node) + length + value_length + 2);
    char **vector = malloc((count + 2) * sizeof(*vector));
    if (!node || !vector) {
        free(node);
        free(vector);
        unlock_environment();
        errno = ENOMEM;
        return -1;
    }
    memcpy(node->text, name, length);
    node->text[length] = '=';
    memcpy(node->text + length + 1, value, value_length + 1);
    for (size_t i = 0; i < count; ++i) vector[i] = environ[i];
    if (found == SIZE_MAX) vector[count++] = node->text;
    else vector[found] = node->text;
    vector[count] = NULL;
    node->next = owned_strings;
    owned_strings = node;
    char **old_vector = owned_vector;
    owned_vector = environ = vector;
    free(old_vector);
    collect_strings();
    unlock_environment();
    errno = saved_errno;
    return 0;
}

int unsetenv(const char *name) {
    size_t length = name_length(name);
    if (!length) { errno = EINVAL; return -1; }
    int saved_errno = errno;
    if (lock_environment()) return -1;
    if (environ) {
        size_t dest = 0;
        for (size_t i = 0; environ[i]; ++i)
            if (!matches(environ[i], name, length)) environ[dest++] = environ[i];
        environ[dest] = NULL;
    }
    collect_strings();
    unlock_environment();
    errno = saved_errno;
    return 0;
}
