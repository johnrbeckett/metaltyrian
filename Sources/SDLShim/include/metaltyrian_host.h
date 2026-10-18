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

/* What the game wants from the Mac pointer. When relative mode is on (during
 * play) the app hides and locks the pointer and sends only movement. */
bool mt_cursor_wanted(void);
bool mt_relative_mouse(void);
void mt_push_quit(void);

/* Gamepads. The game sees two gamepads, "MetalTyrian Gamepad 1" and "2" (one
 * per player; pad is 0 or 1), each with this
 * layout. Axes run -32768..32767, down and right positive. buttons is a bit
 * mask (bit n = button n). hat is SDL_HAT_* bits: up 1, right 2, down 4,
 * left 8. The game's default assignments use axes 0-1, the hat and buttons
 * 0-5, in this order: fire, change fire, left sidekick, right sidekick,
 * menu, pause. Fire and menu also confirm in menus; change fire and pause
 * go back. */
#define MT_PADS 2
#define MT_PAD_AXES 4      /* left x, left y, right x, right y */
#define MT_PAD_BUTTONS 12
#define MT_PAD_A 0         /* fire, confirm */
#define MT_PAD_B 1         /* change fire, back */
#define MT_PAD_LB 2        /* left sidekick */
#define MT_PAD_RB 3        /* right sidekick */
#define MT_PAD_START 4     /* menu */
#define MT_PAD_BACK 5      /* pause */
#define MT_PAD_X 6
#define MT_PAD_Y 7
#define MT_PAD_LT 8
#define MT_PAD_RT 9
#define MT_PAD_L3 10
#define MT_PAD_R3 11
void mt_set_gamepad(int pad, const int16_t *axes, uint32_t buttons, uint8_t hat);

/* Display settings, shared by the game's Graphics menu and the Mac View menu.
 * Either side may change one; the app watches for changes, applies them and
 * saves them. */
#define MT_SETTING_FULLSCREEN 0   /* 0 window, 1 full screen */
#define MT_SETTING_LOOK 1         /* 0 sharp pixels, 1 authentic VGA */
#define MT_SETTING_GLOW 2         /* 0 off, 1-4 = strength 1.0, 1.2, 1.5, 2.0 */
#define MT_SETTING_ASPECT 3       /* 0 original 4:3, 1 square pixels 16:10, 2 exact whole-pixel scale */
#define MT_SETTING_BORDERS 4      /* 0 black, 1 ambient */
#define MT_SETTING_KEY_SPEED 5    /* 1-5, 1 = original: ship's step per frame for keys and d-pad */
#define MT_SETTING_PAD_SPEED 6    /* 1-10: analog stick speed at full tilt */
#define MT_SETTING_MOUSE_SPEED 7  /* 1-10, 5 = original: mouse movement scale (applied by the app) */
#define MT_SETTING_MOMENTUM 8     /* 0 original, 1 light, 2 off: drift from the player's own steering */
#define MT_SETTING_MOUSE_MODE 9   /* 0 original (relative nudges), 1 direct (ship flies to the pointer) */
#define MT_SETTING_TEXT 10        /* 0 crisp, 1 original pixel text */
#define MT_SETTING_COUNT 11
int mt_setting(int which);
void mt_set_setting(int which, int value);

/* Sound. The app plays mono float samples at MT_AUDIO_RATE and calls
 * mt_audio_render from its audio thread for each buffer; it writes silence
 * until the game has opened and started its audio device. */
#define MT_AUDIO_RATE 44100
void mt_audio_render(float *out, int frames);

/* Called by the game (from OpenTyrian's scale_and_flip) for every frame shown. */
void mt_present_frame(const void *pixels, int pitch, const uint32_t *palette);

/* Crisp text. With each frame the game hands over the text visible in it:
 * letters drawn by its fonts and labels painted into HUD art, plus a 320x200
 * mask of the original text pixels for the app to paint out (1 = text). */
#define MT_TEXT_MAX 2048
typedef struct
{
	int16_t x, y;        /* top-left of the letter's cell or the label's box, game pixels */
	uint8_t w, h;        /* cell or box size */
	uint8_t cap;         /* capital height: the baseline is y + cap */
	uint8_t kind;        /* MT_TEXT_LETTER, MT_TEXT_LABEL, MT_TEXT_LABEL_V */
	uint8_t hue, level;  /* body colour is palette entry hue * 16 + level */
	uint8_t font;        /* 0 large, 1 normal, 2 tiny, 3 HUD label */
	uint8_t outline_level;   /* colour of the black outline: hue * 16 + this; 255 = true black */
	/* Darker copies of the letter drawn around it (drop shadows, outlines):
	 * bit (dy + 2) * 5 + (dx + 2) for a copy offset dx, dy game pixels.
	 * dark: the picture darkened in the letter's shape; black: solid black. */
	uint32_t outline_dark, outline_black;
	int16_t clip_y0, clip_y1;   /* only rows y >= clip_y0 and y < clip_y1 show (game pixels); -1 = no limit */
	int16_t clip_x0, clip_x1;   /* likewise for columns */
	char text[14];       /* the letter, or the label text (UTF-8, NUL-terminated) */
} mt_text_item;
#define MT_TEXT_LETTER 0
#define MT_TEXT_LABEL 1      /* a word fitted to its box */
#define MT_TEXT_LABEL_V 2    /* one letter of a vertical label, centred in its box */

/* The game's mouse pointer, taken out of a frame that has crisp text so the
 * app can draw it on top of the text. */
#define MT_CURSOR_W 24
#define MT_CURSOR_H 28
typedef struct
{
	int16_t x, y;        /* top-left, game pixels */
	uint8_t shown;
	uint8_t pad;
	uint8_t pixels[MT_CURSOR_W * MT_CURSOR_H];   /* palette indices */
	uint8_t opaque[MT_CURSOR_W * MT_CURSOR_H];   /* 1 where the pointer is */
} mt_text_cursor;

/* Called by the game just before mt_present_frame: the text in that frame
 * (cursor may be NULL). */
void mt_set_frame_text(const mt_text_item *items, int count, const uint8_t *mask, const mt_text_cursor *cursor);
/* Like mt_copy_frame, and also copies that frame's text (up to max items),
 * mask and pointer; *count gets the number of items. */
uint64_t mt_copy_frame_with_text(uint64_t last_seen, uint8_t *pixels, uint32_t *palette,
                                 mt_text_item *items, int max, int *count, uint8_t *mask,
                                 mt_text_cursor *cursor);

#ifdef __cplusplus
}
#endif

#endif
