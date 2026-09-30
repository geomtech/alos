#include "entropy.h"
#include "uaccess.h"
#include "../drivers/virtio_rng.h"
#include "../include/entropy_abi.h"
#include "../include/errno.h"

int64_t entropy_getentropy(void *user_buffer, size_t length) {
    if (length > ALOS_GETENTROPY_MAX) return -EIO;
    if (!length) return 0;
    if (!user_buffer || !user_range_valid(user_buffer, length, true))
        return -EFAULT;
    uint8_t bytes[ALOS_GETENTROPY_MAX];
    int result = virtio_rng_read(bytes, length);
    /* Revalider apres le sommeil: un autre thread peut changer le mapping. */
    if (!result && copy_to_user(user_buffer, bytes, length)) result = -EFAULT;
    volatile uint8_t *wipe = bytes;
    for (size_t i = 0; i < sizeof(bytes); i++) wipe[i] = 0;
    return result;
}
