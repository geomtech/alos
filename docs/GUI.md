# Desktop graphique ALOS

## Architecture

La GUI officielle est un serveur d'affichage userland multiprocessus :

```text
kernel
  framebuffer + input
  IPC local + mémoire partagée
          |
          v
/bin/gui
  desktop + compositeur + window manager
  launcher + barre des tâches
          |
          v
libgui.a
          |
          +-- /bin/gui-demo
          +-- futures applications ELF
```

`/bin/gui` est le seul processus autorisé à mapper le framebuffer et à lire
les événements matériels. Une application ne reçoit jamais de pointeur vers
la mémoire d'une autre application : elle dessine dans une surface ARGB
allouée par `shm_create()`, puis transmet la capacité SHM au desktop par IPC.

L'ancienne interface se trouve dans `/bin/gui-test`. Elle reste lançable
manuellement lorsqu'aucun serveur d'affichage n'est actif, mais elle n'est
plus l'interface officielle.

## Processus et isolation

Chaque application graphique est un ELF userland normal, lancé avec
`fork()` puis `execve()`. Le desktop ne contient aucun code spécifique à une
application.

Les primitives kernel ajoutées restent génériques :

- services IPC nommés et connexions bidirectionnelles ;
- messages copiés, bornés à 2048 octets ;
- passage contrôlé d'un descripteur SHM ;
- objets SHM paginés et comptés par références ;
- descripteurs IPC/SHM fermés automatiquement lors de `execve()` ;
- déconnexion automatique lors de la terminaison d'un processus ;
- propriété exclusive du framebuffer et de la file d'entrée.

Le desktop associe chaque fenêtre à la connexion IPC attribuée par le kernel.
Un client ne peut donc pas revendiquer une fenêtre appartenant à une autre
connexion. La mort d'un client ferme toutes ses fenêtres sans arrêter le
desktop.

## Fenêtres

Le window manager gère :

- z-order et fenêtre active ;
- focus clavier et événements souris traduits en coordonnées clientes ;
- capture souris pendant un déplacement ou redimensionnement ;
- clipping de la surface dans la zone cliente ;
- barre de titre, bordure et boutons fermer/minimiser/maximiser ;
- états normal, minimisé, maximisé, split gauche et split droite ;
- restauration de la géométrie normale ;
- taskbar avec activation, minimisation et restauration ;
- plusieurs applications et fenêtres simultanées.

Glisser une fenêtre sur le bord gauche ou droit active le split screen.
Le bouton de maximisation restaure également une fenêtre maximisée ou en
split.

## Launcher

Le bouton **Apps** lit dynamiquement les manifestes de
`/share/applications/*.desktop`. Un manifeste minimal contient :

```ini
Name=GUI Demo
Exec=/bin/gui-demo
```

`Exec` doit être un chemin absolu sous `/bin`. Ajouter une application au hub
ne nécessite aucune modification de `/bin/gui`.

## Ajouter une application

1. Inclure `libgui/include/gui.h`.
2. Compiler un ELF userland séparé et le lier avec `libgui/libgui.a` puis
   `libc/libc.a`.
3. Installer l'ELF dans `/bin`.
4. Installer un manifeste dans `/share/applications`.

Exemple :

```c
#include <gui.h>

int main(void) {
  gui_window_t *window =
      gui_create_window("Application", 480, 320, GUI_WINDOW_DEFAULT);
  if (window == 0)
    return 1;

  gui_clear(window, 0xFFF4F7FA);
  gui_present(window, 0);

  for (;;) {
    gui_event_t event;
    if (gui_wait_event(window, &event, 100) < 0)
      break;

    if (event.type == GUI_EVENT_PAINT ||
        event.type == GUI_EVENT_RESIZE) {
      gui_clear(window, 0xFFF4F7FA);
      gui_present(window, 0);
    } else if (event.type == GUI_EVENT_CLOSE) {
      gui_destroy_window(window);
      return 0;
    }
  }
  return 1;
}
```

`/bin/gui-demo` constitue l'implémentation de référence : il reçoit paint,
resize, focus, clavier et souris, puis redessine sa propre surface.

## Limites actuelles

- format de surface unique : ARGB 32 bits ;
- un écran et un serveur d'affichage ;
- compositeur logiciel avec redraw complet ;
- aucun effet animé ou accélération GPU ;
- manifestes volontairement simples (`Name` et `Exec`).

Ces limites ne changent pas le modèle applicatif : les futures applications
restent des processus indépendants utilisant la même ABI.
