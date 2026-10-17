/*
 * MetalTyrian host interface: how the Mac app (Swift) and the game thread
 * (OpenTyrian2000 over the SDL shim) talk to each other. Every function here
 * is safe to call from the main thread while the game thread runs.
 */
#ifndef METALTYRIAN_HOST_H
#define METALTYRIAN_HOST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MT_SCREEN_WIDTH 320
#define MT_SCREEN_HEIGHT 200

/* Starts the game on its own thread. argv[0] is ignored; pass "-t", data dir. */
void mt_start_game(int argc, const char *const *argv);

/* True once the game thread has finished (the game quit). */
bool mt_game_finished(void);

/* Copies the newest frame: 320x200 palette indices and 256 palette entries
 * (0x00RRGGBB). Returns the frame number, 0 until the first frame. Copies only
 * when the frame number differs from last_seen. */
uint64_t mt_copy_frame(uint64_t last_seen, uint8_t *pixels, uint32_t *palette);

/* Input. scancode is a USB HID usage, the same numbers SDL uses. mods is a
 * mask of MT_MOD_*. Coordinates are game screen pixels (0-319, 0-199). */
#define MT_MOD_SHIFT 1
#define MT_MOD_CTRL 2
#define MT_MOD_ALT 4
#define MT_MOD_GUI 8
void mt_push_key(int scancode, bool down, int mods);
void mt_push_text(const char *utf8);
void mt_push_mouse_motion(int x, int y, int dx, int dy);
void mt_push_mouse_button(int button, bool down, int x, int y); /* 1 left, 2 middle, 3 right */
void mt_push_focus(bool focused);
void mt_push_quit(void);

/* Called by the game (from OpenTyrian's scale_and_flip) for every frame shown. */
void mt_present_frame(const void *pixels, int pitch, const uint32_t *palette);

#ifdef __cplusplus
}
#endif

#endif
