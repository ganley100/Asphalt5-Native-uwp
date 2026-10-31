/* UWP (Xbox One / Windows 10 app) host interface.
 *
 * uwp_app.cpp owns the CoreApplication view, the CoreWindow, the EGL
 * context (ANGLE on Direct3D 11) and Windows.Gaming.Input.  The C game host
 * (win32.c, main.c) talks to it only through these functions.
 */
#ifndef ASPHALT5_UWP_HOST_H
#define ASPHALT5_UWP_HOST_H

#include <windows.h>
#include <xinput.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- provided by uwp_app.cpp ------------------------------------------- */
int  uwp_surface_size(int *w, int *h);          /* 0 when the EGL surface exists */
void *uwp_gl_getproc(const char *name);         /* OpenGL ES entry points */
void uwp_swap(void);
void uwp_pump_events(void);
int  uwp_window_closed(void);
int  uwp_pointer(int *x, int *y, int *down);    /* surface pixels; 0 = no pointer */
int  uwp_take_escape(void);                     /* Esc pressed since last call */
DWORD WINAPI uwp_xinput_get_state(DWORD index, XINPUT_STATE *state);
void uwp_message_box(const char *text, const char *title);

/* Draw a progress screen (frac < 0 hides the bar) and present it. */
void uwp_progress(float frac);

/* ---- provided by the game host ----------------------------------------- */
void platform_key_event(int vk, int down);      /* win32.c */
void platform_focus_lost(void);                 /* win32.c */
int  recomp_main(int argc, char **argv);        /* main.c */

/* uwp_import.c: unpack the user's APK and game data into LocalState.
 * Returns 0 when the game files are ready, otherwise an error message. */
const char *uwp_import_game_files(const wchar_t *local_dir, const wchar_t *install_dir);

#ifdef __cplusplus
}
#endif

#endif /* ASPHALT5_UWP_HOST_H */
