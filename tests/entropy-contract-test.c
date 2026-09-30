/* Test hote du contrat uniquement; aucun octet de test n'est une entropie. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../src/kernel/entropy.h"
#include "../src/include/errno.h"

static uint8_t output[256];
static int writable = 1, source_error, copy_error, reads, copies;
bool user_range_valid(const void *p, size_t n, bool write) {
    return writable && p == output && n <= sizeof(output) && write;
}
int copy_to_user(void *p, const void *bytes, size_t n) {
    copies++;
    if (copy_error) return -1;
    memcpy(p, bytes, n);
    return 0;
}
int virtio_rng_read(void *p, size_t n) {
    reads++;
    if (source_error) return source_error;
    memset(p, 0x5a, n);
    return 0;
}

int main(void) {
    assert(entropy_getentropy(NULL, 0) == 0 && !reads && !copies);
    assert(entropy_getentropy(output, 257) == -EIO && !reads);
    assert(entropy_getentropy(NULL, 1) == -EFAULT && !reads);
    assert(entropy_getentropy((void *)(uintptr_t)-1, 1) == -EFAULT && !reads);
    writable = 0;
    assert(entropy_getentropy(output, 256) == -EFAULT && !reads);
    writable = 1;
    memset(output, 0xa5, sizeof(output));
    source_error = -ENOSYS;
    assert(entropy_getentropy(output, 256) == -ENOSYS && !copies);
    for (unsigned i = 0; i < sizeof(output); i++) assert(output[i] == 0xa5);
    source_error = -EIO;
    assert(entropy_getentropy(output, 256) == -EIO && !copies);
    source_error = 0;
    assert(entropy_getentropy(output, 256) == 0 && copies == 1);
    for (unsigned i = 0; i < sizeof(output); i++) assert(output[i] == 0x5a);
    copy_error = 1;
    assert(entropy_getentropy(output, 256) == -EFAULT);
    return 0;
}
