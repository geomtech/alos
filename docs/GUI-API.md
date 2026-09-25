# API graphique userland ALOS

## `gui.h`

### Fenêtre

```c
gui_window_t *gui_create_window(const char *title,
                                uint32_t width,
                                uint32_t height,
                                uint32_t flags);
void gui_destroy_window(gui_window_t *window);
int gui_set_title(gui_window_t *window, const char *title);
```

Les dimensions demandées concernent la zone cliente. Les flags disponibles
sont :

- `GUI_WINDOW_RESIZABLE`
- `GUI_WINDOW_MINIMIZABLE`
- `GUI_WINDOW_MAXIMIZABLE`
- `GUI_WINDOW_DEFAULT`

### Surface

```c
uint32_t *gui_window_pixels(gui_window_t *window);
uint32_t gui_window_width(const gui_window_t *window);
uint32_t gui_window_height(const gui_window_t *window);
uint32_t gui_window_stride(const gui_window_t *window);
int gui_present(gui_window_t *window, const gui_rect_t *damage);
```

La surface est ARGB 32 bits. `gui_present(window, NULL)` publie toute la
surface. Un rectangle de dommage hors surface est rejeté par le desktop.

Lors d'un resize, `libgui` crée et attache une nouvelle surface avant de
livrer `GUI_EVENT_RESIZE` à l'application. Le pointeur retourné précédemment
ne doit plus être utilisé après cet événement.

### Événements

```c
int gui_poll_event(gui_window_t *window, gui_event_t *event);
int gui_wait_event(gui_window_t *window, gui_event_t *event,
                   uint32_t timeout_ms);
```

Types :

- `GUI_EVENT_PAINT`
- `GUI_EVENT_RESIZE`
- `GUI_EVENT_FOCUS`
- `GUI_EVENT_MOUSE_MOVE`
- `GUI_EVENT_MOUSE_DOWN`
- `GUI_EVENT_MOUSE_UP`
- `GUI_EVENT_KEY_DOWN`
- `GUI_EVENT_KEY_UP`
- `GUI_EVENT_CLOSE`

Les coordonnées souris sont relatives à la zone cliente. Les interactions
avec les décorations ne sont pas envoyées à l'application.

### Dessin minimal

```c
void gui_clear(gui_window_t *window, uint32_t color);
void gui_draw_pixel(gui_window_t *window, int32_t x, int32_t y,
                    uint32_t color);
void gui_fill_rect(gui_window_t *window, gui_rect_t rect, uint32_t color);
void gui_draw_rect(gui_window_t *window, gui_rect_t rect, uint32_t color);
```

Ces fonctions écrivent uniquement dans la surface du client. Elles
n'effectuent pas de présentation implicite.

## Protocole

`gui_protocol.h` définit une ABI versionnée (`GUI_PROTOCOL_VERSION`) composée
uniquement d'entiers de taille fixe et de tableaux intégrés. Aucun pointeur
userland ne traverse l'IPC.

Messages client vers desktop :

- `GUI_MSG_HELLO`
- `GUI_MSG_CREATE_WINDOW`
- `GUI_MSG_ATTACH_SURFACE` avec descripteur SHM
- `GUI_MSG_PRESENT`
- `GUI_MSG_DESTROY_WINDOW`
- `GUI_MSG_SET_TITLE`

Messages desktop vers client :

- `GUI_MSG_HELLO_ACK`
- `GUI_MSG_WINDOW_CREATED`
- `GUI_MSG_EVENT`
- `GUI_MSG_ERROR`

Le desktop valide la version, la taille exacte du message, le propriétaire
de la fenêtre, les dimensions, le stride, la taille réelle de l'objet SHM et
les rectangles de dommage.

## API système sous-jacente

Les headers libc `sys/ipc.h`, `sys/shm.h` et `sys/display.h` exposent les
primitives génériques utilisées par `libgui`. Les applications graphiques
ordinaires ne doivent pas acquérir l'affichage ni implémenter directement le
protocole : elles utilisent `libgui`.
