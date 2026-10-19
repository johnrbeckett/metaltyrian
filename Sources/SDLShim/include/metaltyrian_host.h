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
/* The version shown on the title screen ("Metal 0.1.0"); set before mt_start_game. */
void mt_set_title_version(const char *text);
const char *mt_title_version(void);

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
#define MT_SETTING_STEREO 11      /* 0 stereo (effects placed by position), 1 mono */
#define MT_SETTING_LIGHT 12       /* dynamic light from shots and explosions: 0 off, 1 subtle, 2 medium, 3 strong */
#define MT_SETTING_EDGES 13       /* edge smoothing on the playfield: 0 off, 1 soft, 2 smooth */
#define MT_SETTING_AUTOFIRE 14    /* bit 0 main guns, bit 1 sidekicks */
#define MT_SETTING_KEYS 15        /* 22 settings: each action's two keys (SDL scancodes, 0 = default) */
#define MT_SETTING_AUTOFIRE_SPECIALS 37  /* 1: quick specials fire by themselves (varz.c) */
#define MT_SETTING_COUNT 38

/* Keys (Setup > Controls > Keys): each action has two keys, slot 0 the
 * right-handed default and slot 1 the left-handed one. */
#define MT_ACT_UP 0
#define MT_ACT_DOWN 1
#define MT_ACT_LEFT 2
#define MT_ACT_RIGHT 3
#define MT_ACT_FIRE 4
#define MT_ACT_SIDEKICKS 5
#define MT_ACT_REAR 6
#define MT_ACT_BOMB 7
#define MT_ACT_AUTOFIRE_MAIN 8
#define MT_ACT_AUTOFIRE_SIDEKICKS 9
#define MT_ACT_AUTOFIRE_SPECIALS 10
#define MT_ACT_COUNT 11
int mt_key_default(int action, int slot);
int mt_key_binding(int action, int slot);      /* the scancode in use */
void mt_set_key_binding(int action, int slot, int scancode);   /* 0 = back to the default */
const char *mt_key_name(int scancode);          /* short, for menus and the overlay */
int mt_setting(int which);
void mt_set_setting(int which, int value);

/* Sound. The app plays stereo float samples at MT_AUDIO_RATE (the AdLib
 * chip's own rate, so the music needs no resampling; macOS converts to the
 * output device) and calls mt_audio_render from its audio thread for each
 * buffer; it writes silence until the game has opened and started its audio
 * device. */
#define MT_AUDIO_RATE 49716
void mt_audio_render(float *left, float *right, int frames);

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

/* A full-screen picture in high resolution: the title (John's art, with its
 * two logos) or another tyrian.pic picture with replacement art. Says whether
 * the frame shows it, which, where the title's logos are (game pixels, the
 * original sprites' boxes), and which frame pixels are drawn on top of the
 * picture (menu, HUD bars, pointer). */
typedef struct
{
	uint8_t shown;
	uint8_t logo_shown[2];          /* 0 TYRIAN, 1 the year */
	uint8_t picture;                /* 0: the title art; else the tyrian.pic number */
	int16_t logo_x[2], logo_y[2], logo_w[2], logo_h[2];
	uint32_t brightness;            /* the picture's palette sum, entries 0-239, unfaded */
	uint16_t fade;                  /* the picture's palette fade, 4096 = full */
	uint8_t smooth;                 /* 1: smooth what is drawn on top (HUD bars) */
	uint8_t pad;
	int16_t hole[4];                /* x, y, w, h: the game's own picture shows here (the playfield) */
	uint8_t controls;               /* the controls overlay's opacity, 0-255 (level start, pause) */
	uint8_t controls_lefty;         /* bit 0: the left-handed keys; bit 1: "Press fire to start" */
} mt_title_frame;
/* NULL: not the title screen. foreground: 320x200, 1 where the frame has
 * something over the picture, 2-255 where the picture is only shaded darker
 * (2 + darkness, 253 = black) (kept apart: Swift can't import so large an array). */
void mt_set_frame_title(const mt_title_frame *title, const uint8_t *foreground);

/* Edge smoothing: for the next frame, per game pixel (320x200), the layer the
 * pixel was last drawn in (0 = the bottom background, higher = drawn later)
 * and the palette index that pixel had before that layer covered it. NULL:
 * none for this frame. */
void mt_set_frame_edges(const uint8_t *layer, const uint8_t *under);
/* The edges of the frame mt_copy_frame_with_text last copied: 1 and filled
 * in, or 0 when that frame had none. Call from the thread that copies. */
int mt_copied_frame_edges(uint8_t *layer, uint8_t *under);

/* Called by the game just before mt_present_frame: the text in that frame
 * (cursor may be NULL). */
void mt_set_frame_text(const mt_text_item *items, int count, const uint8_t *mask, const mt_text_cursor *cursor);
/* Like mt_copy_frame, and also copies that frame's text (up to max items),
 * mask and pointer; *count gets the number of items. */
uint64_t mt_copy_frame_with_text(uint64_t last_seen, uint8_t *pixels, uint32_t *palette,
                                 mt_text_item *items, int max, int *count, uint8_t *mask,
                                 mt_text_cursor *cursor, mt_title_frame *title,
                                 uint8_t *title_foreground);

#ifdef __cplusplus
}
#endif

#endif
