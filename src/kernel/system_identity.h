#ifndef SYSTEM_IDENTITY_H
#define SYSTEM_IDENTITY_H

#include "../include/utsname.h"
#include <stdint.h>

int64_t sys_uname(struct utsname *user_name);

#endif
