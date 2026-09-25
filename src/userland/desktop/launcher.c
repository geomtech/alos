/* src/userland/desktop/launcher.c - Decouverte et lancement des applications */
#include "desktop.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define APPLICATIONS_DIR "/share/applications"

static gui_rect_t launcher_rect(void) {
  uint32_t height = 48 + g_desktop.app_count * 32U;
  if (height < 80) height = 80;
  return (gui_rect_t){8,
                      (int32_t)g_desktop.framebuffer.height -
                          DESKTOP_TASKBAR - (int32_t)height - 4,
                      300, height};
}

static bool point_in_rect(int32_t x, int32_t y, gui_rect_t rect) {
  return x >= rect.x && y >= rect.y &&
         x < rect.x + (int32_t)rect.width &&
         y < rect.y + (int32_t)rect.height;
}

static void trim_line(char *line) {
  size_t length = strlen(line);
  while (length > 0 &&
         (line[length - 1] == '\r' || line[length - 1] == '\n' ||
          line[length - 1] == ' ' || line[length - 1] == '\t')) {
    line[--length] = '\0';
  }
}

static bool parse_manifest(const char *path, desktop_app_t *app) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;
  char buffer[512];
  int length = (int)read(fd, buffer, sizeof(buffer) - 1);
  close(fd);
  if (length <= 0) return false;
  buffer[length] = '\0';

  memset(app, 0, sizeof(*app));
  char *cursor = buffer;
  while (*cursor != '\0') {
    char *line = cursor;
    while (*cursor != '\0' && *cursor != '\n') cursor++;
    if (*cursor == '\n') *cursor++ = '\0';
    trim_line(line);
    if (strncmp(line, "Name=", 5) == 0) {
      strncpy(app->name, line + 5, sizeof(app->name) - 1);
    } else if (strncmp(line, "Exec=", 5) == 0) {
      strncpy(app->executable, line + 5, sizeof(app->executable) - 1);
    }
  }
  return app->name[0] != '\0' &&
         strncmp(app->executable, "/bin/", 5) == 0;
}

void desktop_load_apps(void) {
  g_desktop.app_count = 0;
  for (unsigned int index = 0; index < 128 &&
                               g_desktop.app_count < DESKTOP_MAX_APPS;
       index++) {
    struct dirent entry;
    int result = readdir(APPLICATIONS_DIR, index, &entry);
    if (result == 1) break;
    if (result != 0 || entry.d_type != DT_FILE) continue;

    size_t name_length = strlen(entry.d_name);
    if (name_length < 8 ||
        strcmp(entry.d_name + name_length - 8, ".desktop") != 0) {
      continue;
    }
    char path[256];
    int written = snprintf(path, sizeof(path), "%s/%s", APPLICATIONS_DIR,
                           entry.d_name);
    if (written <= 0 || (size_t)written >= sizeof(path)) continue;
    if (parse_manifest(path, &g_desktop.apps[g_desktop.app_count])) {
      g_desktop.app_count++;
    }
  }
}

void desktop_launch_app(uint32_t index) {
  if (index >= g_desktop.app_count) return;
  pid_t child = fork();
  if (child > 0) {
    g_desktop.child_count++;
    return;
  }
  if (child < 0) return;

  char *argv[2] = {g_desktop.apps[index].executable, NULL};
  execve(g_desktop.apps[index].executable, argv, NULL);
  _exit(127);
}

bool desktop_handle_shell_click(int32_t x, int32_t y) {
  int32_t taskbar_y =
      (int32_t)g_desktop.framebuffer.height - DESKTOP_TASKBAR;
  if (y >= taskbar_y) {
    if (x >= 8 && x < 90) {
      g_desktop.launcher_open = !g_desktop.launcher_open;
      desktop_mark_dirty();
      return true;
    }

    int32_t item_x = 100;
    for (uint32_t i = 0; i < g_desktop.z_count; i++) {
      desktop_window_t *window = g_desktop.z_order[i];
      if (window == NULL || !window->used) continue;
      if (x >= item_x && x < item_x + 120) {
        if (window == g_desktop.active && !window->minimized) {
          desktop_minimize_window(window);
        } else {
          desktop_focus_window(window);
        }
        desktop_mark_dirty();
        return true;
      }
      item_x += 126;
    }
    return true;
  }

  if (g_desktop.launcher_open) {
    gui_rect_t panel = launcher_rect();
    if (point_in_rect(x, y, panel)) {
      int32_t relative = y - panel.y - 42;
      if (relative >= 0) {
        uint32_t index = (uint32_t)relative / 32U;
        if (index < g_desktop.app_count) {
          desktop_launch_app(index);
          g_desktop.launcher_open = false;
          desktop_mark_dirty();
        }
      }
      return true;
    }
    g_desktop.launcher_open = false;
    desktop_mark_dirty();
  }
  return false;
}
