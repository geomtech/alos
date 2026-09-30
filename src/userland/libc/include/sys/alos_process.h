#ifndef _SYS_ALOS_PROCESS_H
#define _SYS_ALOS_PROCESS_H
#ifdef __cplusplus
extern "C" {
#endif
/* Modifie le nom de debug natif (31 octets maximum), pas les arguments ELF. */
int alos_set_process_title(const char *title);
#ifdef __cplusplus
}
#endif
#endif
