/* ABI native x86-64 : cinq tableaux contigus, sans pointeur ni padding. */
#ifndef ALOS_UTSNAME_H
#define ALOS_UTSNAME_H

#define ALOS_SYS_UNAME 260
#define ALOS_UTSNAME_FIELD_SIZE 65

struct utsname {
  char sysname[ALOS_UTSNAME_FIELD_SIZE];
  char nodename[ALOS_UTSNAME_FIELD_SIZE];
  char release[ALOS_UTSNAME_FIELD_SIZE];
  char version[ALOS_UTSNAME_FIELD_SIZE];
  char machine[ALOS_UTSNAME_FIELD_SIZE];
};

typedef char alos_utsname_size_check[
    sizeof(struct utsname) == 5 * ALOS_UTSNAME_FIELD_SIZE ? 1 : -1];

#endif
