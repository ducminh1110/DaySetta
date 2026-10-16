/*
 * Desktop shell: the menu bar (top) and the Dock (bottom) that Daylight draws itself.
 *
 * There is no Dock.app, SystemUIServer or Finder in a BaseSystem image (they only exist on a full macOS volume),
 * so the window server draws the desktop chrome on top of the composited windows: a translucent menu bar with the
 * front application's name, a live clock and status icons, and a Dock with one tile per running application.
 * Clicking a Dock tile brings that application to the front. Everything here is the server's own drawing; when a
 * real system UI is available it can replace this.
 */
#ifndef DAYLIGHT_SHELL_H
#define DAYLIGHT_SHELL_H

#include <stdint.h>

struct dl_canvases;

#define DL_MENU_BAR_HEIGHT 26
#define DL_DOCK_ITEM 52
#define DL_DOCK_GAP 12
#define DL_DOCK_PANEL_HEIGHT 68
#define DL_DOCK_BOTTOM_MARGIN 6

/* 1 only when DSWS_DEBUG_CHROME=1: menu bar, Dock and wallpaper are labelled "DEBUG CHROME - not Apple UI". Default 0: clients on black. */
int dl_shell_debug_chrome(void);

void dl_shell_init(uint32_t screen_width, uint32_t screen_height);

/* The process the menu bar names and the Dock highlights. */
void dl_shell_set_front(int32_t pid);

/* 1 when the shell wants a redraw (the clock moved on, or the shell changed); cleared by dl_shell_draw. */
int dl_shell_needs_redraw(void);

/* Draws the menu bar and the Dock over the composited screen (as on macOS, both float above the windows). */
void dl_shell_draw(const struct dl_canvases *canvases, uint8_t *rgba);

/* Pointer motion: updates the Dock hover highlight. */
void dl_shell_motion(float x, float y);

/*
 * A mouse button press at (x, y). Returns 1 when the shell consumed the click (menu bar or Dock); *focus_pid is
 * set to the process of the Dock tile that was clicked, or 0 when the click was on the menu bar or empty Dock.
 */
int dl_shell_press(float x, float y, uint32_t button, int32_t *focus_pid);

#endif
