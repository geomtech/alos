/* src/kernel/display.h - Propriete exclusive du serveur d'affichage */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

void display_init(void);
int display_acquire(uint32_t pid);
int display_release(uint32_t pid);
void display_release_if_owner(uint32_t pid);
bool display_is_owner(uint32_t pid);

#endif
