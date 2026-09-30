#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures, constructor_saw_environment;
static void __attribute__((constructor)) probe_environment(void) {
    const char *value = getenv("ALOS_ENV_EXEC");
    constructor_saw_environment = value && !strcmp(value, "inherited");
}
#define CHECK(expr) do { if (!(expr)) { \
    printf("env-test: FAIL line %d errno=%d\n", __LINE__, errno); ++failures; \
} } while (0)
static int value_is(const char *name, const char *expected) {
    const char *value = getenv(name);
    return value && !strcmp(value, expected);
}

static void *worker(void *argument) {
    const char *name = argument;
    for (int i = 0; i < 100; ++i) {
        char value[32];
        snprintf(value, sizeof(value), "%d", i);
        if (setenv(name, value, 1) || !value_is(name, value)) return (void *)1;
        /* Chaque thread ne modifie que sa variable : emprunt utilise avant unset. */
        if (unsetenv(name) || getenv(name)) return (void *)1;
    }
    return NULL;
}

static void check_rollback(char *const envp[]) {
    char before[256], after[256];
    CHECK(getcwd(before, sizeof(before)) != NULL);
    int fd = open("/config/startup.sh", O_RDONLY);
    CHECK(fd >= 0);
    char *args[] = {"/bin/env-test", "exec-inherited", NULL};
    CHECK(execve("/bin/env-test", args, envp) < 0);
    CHECK(getcwd(after, sizeof(after)) != NULL && !strcmp(before, after));
    CHECK(value_is("ALOS_ENV_EXEC", "inherited"));
    if (fd >= 0) {
        char byte;
        CHECK(read(fd, &byte, 1) == 1);
        CHECK(close(fd) == 0);
    }
}

static void check_exec(int empty) {
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        char *args[] = {"/bin/env-test", empty ? "exec-empty" : "exec-inherited", NULL};
        char *empty_env[] = {NULL};
        execve(args[0], args, empty ? empty_env : environ);
        puts("env-test: FAIL exec returned");
        _exit(1);
    }
    if (child > 0) {
        int status = -1;
        CHECK(waitpid(child, &status, 0) == child && status == 0);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "exec-inherited")) {
        CHECK(constructor_saw_environment);
        CHECK(value_is("ALOS_ENV_EXEC", "inherited"));
        CHECK(value_is("ALOS_ENV_EMPTY", ""));
        CHECK(!getenv("ALOS_ENV_REMOVED"));
        if (!failures) puts("env-test: exec inherited constructor PASS");
        return failures != 0;
    }
    if (argc > 1 && !strcmp(argv[1], "exec-empty")) {
        CHECK(!constructor_saw_environment && environ && !environ[0]);
        CHECK(!getenv("ALOS_ENV_EXEC"));
        if (!failures) puts("env-test: exec empty PASS");
        return failures != 0;
    }
    CHECK(environ != NULL);
    errno = EDOM;
    CHECK(!getenv("ALOS_ENV_MISSING") && errno == EDOM);
    CHECK(unsetenv("ALOS_ENV_MISSING") == 0 && errno == EDOM);
    errno = 0;
    CHECK(setenv("", "x", 1) == -1 && errno == EINVAL);
    CHECK(setenv("a=b", "x", 1) == -1 && errno == EINVAL);
    CHECK(unsetenv("") == -1 && errno == EINVAL);
    CHECK(unsetenv("a=b") == -1 && errno == EINVAL);
    char **saved_environment = environ;
    char external_a[] = "ALOS_ENV_DUP=first", external_b[] = "ALOS_ENV_DUP=second";
    char *external[] = {external_a, external_b, NULL};
    environ = external;
    CHECK(value_is("ALOS_ENV_DUP", "first"));
    CHECK(!unsetenv("ALOS_ENV_DUP") && !external[0]);
    environ = saved_environment;
    char name[] = "ALOS_ENV_COPY", value[] = "copied";
    CHECK(!setenv(name, value, 1));
    name[0] = value[0] = 'x';
    CHECK(value_is("ALOS_ENV_COPY", "copied"));
    errno = EDOM;
    CHECK(!setenv("ALOS_ENV_COPY", "ignored", 0) && errno == EDOM);
    CHECK(value_is("ALOS_ENV_COPY", "copied"));
    CHECK(!setenv("ALOS_ENV_COPY", getenv("ALOS_ENV_COPY"), 1));
    CHECK(value_is("ALOS_ENV_COPY", "copied"));
    CHECK(!setenv("ALOS_ENV_COPY", "replacement", 1));
    CHECK(value_is("ALOS_ENV_COPY", "replacement"));
    CHECK(!unsetenv("ALOS_ENV_COPY") && !getenv("ALOS_ENV_COPY"));
    CHECK(!setenv("ALOS_ENV_EXEC", "inherited", 1));
    CHECK(!setenv("ALOS_ENV_EMPTY", "", 1));
    CHECK(!setenv("ALOS_ENV_REMOVED", "x", 1) && !unsetenv("ALOS_ENV_REMOVED"));
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        if (!value_is("ALOS_ENV_EXEC", "inherited") ||
            setenv("ALOS_ENV_EXEC", "child", 1) ||
            !value_is("ALOS_ENV_EXEC", "child")) _exit(1);
        _exit(0);
    }
    if (child > 0) {
        int status = -1;
        CHECK(waitpid(child, &status, 0) == child && status == 0);
        CHECK(value_is("ALOS_ENV_EXEC", "inherited"));
    }
    check_exec(0);
    check_exec(1);
    char *bad_strings[] = {(char *)(uintptr_t)1, NULL};
    check_rollback(bad_strings);
    check_rollback((char *const *)(uintptr_t)1);
    char *good_args[] = {"/bin/env-test", NULL};
    CHECK(execve("/bin/does-not-exist-env", good_args, environ) < 0);
    CHECK(value_is("ALOS_ENV_EXEC", "inherited"));
    CHECK(execve("/bin/env-test", (char *const *)(uintptr_t)1, environ) < 0);
    CHECK(value_is("ALOS_ENV_EXEC", "inherited"));
    char *too_many[66];
    for (size_t i = 0; i < 65; ++i) too_many[i] = "X=1";
    too_many[65] = NULL;
    check_rollback(too_many);
    char *huge = malloc(16385);
    CHECK(huge != NULL);
    if (huge) {
        memset(huge, 'A', 16384);
        huge[16384] = 0;
        char *too_large[] = {huge, NULL};
        check_rollback(too_large);
        free(huge);
    }
    pthread_t threads[4];
    char *names[] = {"ALOS_ENV_T0", "ALOS_ENV_T1", "ALOS_ENV_T2", "ALOS_ENV_T3"};
    int started = 0;
    for (; started < 4; ++started) {
        int error = pthread_create(&threads[started], NULL, worker, names[started]);
        CHECK(error == 0);
        if (error) break;
    }
    for (int i = 0; i < started; ++i) {
        void *result = (void *)1;
        CHECK(!pthread_join(threads[i], &result) && !result);
    }
    if (failures) { printf("env-test: FAIL %d\n", failures); return 1; }
    puts("env-test: PASS");
    return 0;
}
