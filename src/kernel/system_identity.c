#include "system_identity.h"
#include "uaccess.h"
#include "../include/errno.h"

/*
 * Aucun hostname configurable ni numero de release stable n'existe encore.
 * "alos" est l'identite locale par defaut, pas une configuration DNS.
 * Les champs development ne pretendent pas identifier un noyau Linux.
 */
static const struct utsname system_identity = {
    .sysname = "ALOS",
    .nodename = "alos",
    .release = "development",
    .version = "ALOS development",
    .machine = "x86_64",
};

int64_t sys_uname(struct utsname *user_name) {
  if (copy_to_user(user_name, &system_identity, sizeof(system_identity)) != 0)
    return -EFAULT;
  return 0;
}
