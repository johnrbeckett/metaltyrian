/*
 * MetalTyrian SDL shim. Implements the SDL 2 subset declared in SDL.h for
 * OpenTyrian2000, plus the host interface in metaltyrian_host.h.
 *
 * Threads: the game runs on its own thread (mt_start_game) and calls the SDL
 * functions. The Mac app's main thread pushes input with mt_push_* and pulls
 * frames with mt_copy_frame. The event queue and the frame buffer are the
 * only shared state, each behind its own mutex.
 *
 * GPLv2 or later, as the rest of MetalTyrian.
 */
#include "SDL.h"
#include "metaltyrian_host.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ---- Errors, init, misc --------------------------------------------------- */

static char error_text[256] = "";
static Uint32 initialized = 0;

const char *SDL_GetError(void) { return error_text; }

int SDL_SetError(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(error_text, sizeof(error_text), fmt, ap);
	va_end(ap);
	return -1;
}

int SDL_InitSubSystem(Uint32 flags)
{
	initialized |= flags;
	return 0;
}

int SDL_Init(Uint32 flags) { return SDL_InitSubSystem(flags); }
void SDL_QuitSubSystem(Uint32 flags) { initialized &= ~flags; }
Uint32 SDL_WasInit(Uint32 flags) { return flags ? (initialized & flags) : initialized; }
void SDL_Quit(void) { initialized = 0; }

SDL_bool SDL_SetHint(const char *name, const char *value)
{
	(void)name;
	(void)value;
	return SDL_TRUE;
}

size_t SDL_strlcpy(char *dst, const char *src, size_t maxlen)
{
	size_t len = strlen(src);
	if (maxlen > 0)
	{
		size_t n = len < maxlen - 1 ? len : maxlen - 1;
		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}

/* ---- Timing --------------------------------------------------------------- */

static Uint64 now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (Uint64)ts.tv_sec * 1000000000ull + (Uint64)ts.tv_nsec;
}

static Uint64 start_ns = 0;

Uint32 SDL_GetTicks(void)
{
	if (start_ns == 0)
		start_ns = now_ns();
	return (Uint32)((now_ns() - start_ns) / 1000000ull);
}

void SDL_Delay(Uint32 ms)
{
	struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
		;
}

/* ---- Pixels and surfaces -------------------------------------------------- */

static SDL_PixelFormat format8 = { 0, 8, 1 };
static SDL_PixelFormat format32 = { SDL_PIXELFORMAT_RGB888, 32, 4 };

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	(void)flags; (void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
	if (depth != 8 && depth != 32)
	{
		SDL_SetError("unsupported surface depth %d", depth);
		return NULL;
	}
	SDL_Surface *s = calloc(1, sizeof(*s));
	if (s == NULL)
		return NULL;
	s->format = depth == 8 ? &format8 : &format32;
	s->w = width;
	s->h = height;
	s->pitch = width * s->format->BytesPerPixel;
	s->pixels = calloc((size_t)height, (size_t)s->pitch);
	if (s->pixels == NULL)
	{
		free(s);
		return NULL;
	}
	return s;
}

void SDL_FreeSurface(SDL_Surface *surface)
{
	if (surface == NULL)
		return;
	free(surface->pixels);
	free(surface);
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
	SDL_Rect r = { 0, 0, dst->w, dst->h };
	if (rect != NULL)
	{
		r = *rect;
		if (r.x < 0) { r.w += r.x; r.x = 0; }
		if (r.y < 0) { r.h += r.y; r.y = 0; }
		if (r.x + r.w > dst->w) r.w = dst->w - r.x;
		if (r.y + r.h > dst->h) r.h = dst->h - r.y;
		if (r.w <= 0 || r.h <= 0)
			return 0;
	}
	for (int y = r.y; y < r.y + r.h; ++y)
	{
		Uint8 *row = (Uint8 *)dst->pixels + (size_t)y * (size_t)dst->pitch;
		if (dst->format->BytesPerPixel == 1)
		{
			memset(row + r.x, (int)(color & 0xFF), (size_t)r.w);
		}
		else
		{
			Uint32 *p = (Uint32 *)row + r.x;
			for (int x = 0; x < r.w; ++x)
				p[x] = color;
		}
	}
	return 0;
}

SDL_PixelFormat *SDL_AllocFormat(Uint32 pixel_format)
{
	SDL_PixelFormat *f = calloc(1, sizeof(*f));
	if (f == NULL)
		return NULL;
	f->format = pixel_format;
	f->BitsPerPixel = pixel_format == SDL_PIXELFORMAT_RGB565 ? 16 : 32;
	f->BytesPerPixel = pixel_format == SDL_PIXELFORMAT_RGB565 ? 2 : 4;
	return f;
}

void SDL_FreeFormat(SDL_PixelFormat *format) { free(format); }

const char *SDL_GetPixelFormatName(Uint32 format)
{
	return format == SDL_PIXELFORMAT_RGB565 ? "SDL_PIXELFORMAT_RGB565" : "SDL_PIXELFORMAT_RGB888";
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
	if (format != NULL && format->format == SDL_PIXELFORMAT_RGB565)
		return (Uint32)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	return ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
}

/* ---- Window, renderer, texture: bookkeeping only --------------------------- */

struct SDL_Window { int w, h; Uint32 flags; };
struct SDL_Renderer { int unused; };
struct SDL_Texture { Uint32 format; int w, h, pitch; void *pixels; };

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	(void)title; (void)x; (void)y;
	SDL_Window *win = calloc(1, sizeof(*win));
	if (win != NULL)
	{
		win->w = w;
		win->h = h;
		win->flags = flags;
	}
	return win;
}

void SDL_DestroyWindow(SDL_Window *window) { free(window); }
void SDL_ShowWindow(SDL_Window *window) { (void)window; }

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
	if (w) *w = window ? window->w : MT_SCREEN_WIDTH;
	if (h) *h = window ? window->h : MT_SCREEN_HEIGHT;
}

void SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
	if (window)
	{
		window->w = w;
		window->h = h;
	}
}

void SDL_SetWindowPosition(SDL_Window *window, int x, int y) { (void)window; (void)x; (void)y; }

int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags)
{
	if (window)
		window->flags = (window->flags & ~SDL_WINDOW_FULLSCREEN_DESKTOP) | flags;
	return 0;
}

int SDL_GetWindowDisplayIndex(SDL_Window *window) { (void)window; return 0; }
int SDL_GetNumVideoDisplays(void) { return 1; }

int SDL_GetDisplayBounds(int displayIndex, SDL_Rect *rect)
{
	(void)displayIndex;
	rect->x = 0;
	rect->y = 0;
	rect->w = 1920;
	rect->h = 1200;
	return 0;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	(void)window; (void)index; (void)flags;
	return calloc(1, sizeof(SDL_Renderer));
}

void SDL_DestroyRenderer(SDL_Renderer *renderer) { free(renderer); }

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	(void)renderer; (void)r; (void)g; (void)b; (void)a;
	return 0;
}

int SDL_RenderClear(SDL_Renderer *renderer) { (void)renderer; return 0; }

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
	(void)renderer; (void)texture; (void)srcrect; (void)dstrect;
	return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer) { (void)renderer; }

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h)
{
	(void)renderer; (void)access;
	SDL_Texture *t = calloc(1, sizeof(*t));
	if (t == NULL)
		return NULL;
	t->format = format;
	t->w = w;
	t->h = h;
	t->pitch = w * (format == SDL_PIXELFORMAT_RGB565 ? 2 : 4);
	t->pixels = calloc((size_t)h, (size_t)t->pitch);
	if (t->pixels == NULL)
	{
		free(t);
		return NULL;
	}
	return t;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
	if (texture == NULL)
		return;
	free(texture->pixels);
	free(texture);
}

int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h)
{
	if (format) *format = texture->format;
	if (access) *access = SDL_TEXTUREACCESS_STREAMING;
	if (w) *w = texture->w;
	if (h) *h = texture->h;
	return 0;
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
	(void)rect;
	*pixels = texture->pixels;
	*pitch = texture->pitch;
	return 0;
}

void SDL_UnlockTexture(SDL_Texture *texture) { (void)texture; }

/* Set by the game thread, read by the app on the main thread. */
static _Atomic int cursor_shown = 1;
static _Atomic int relative_mouse = 0;

int SDL_ShowCursor(int toggle)
{
	if (toggle >= 0)
		cursor_shown = toggle ? 1 : 0;
	return cursor_shown;
}

int SDL_SetRelativeMouseMode(SDL_bool enabled)
{
	relative_mouse = enabled ? 1 : 0;
	return 0;
}

bool mt_cursor_wanted(void) { return cursor_shown != 0; }

/* ---- Display settings shared with the app ---------------------------------- */

static _Atomic int settings[MT_SETTING_COUNT];

int mt_setting(int which)
{
	return which >= 0 && which < MT_SETTING_COUNT ? settings[which] : 0;
}

void mt_set_setting(int which, int value)
{
	if (which >= 0 && which < MT_SETTING_COUNT)
		settings[which] = value;
}
bool mt_relative_mouse(void) { return relative_mouse != 0; }

/* ---- Keyboard names ------------------------------------------------------- */

static const char *scancode_name(int sc)
{
	static char buf[2];
	if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
	{
		buf[0] = (char)('A' + (sc - SDL_SCANCODE_A));
		buf[1] = '\0';
		return buf;
	}
	if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
	{
		buf[0] = (char)('1' + (sc - SDL_SCANCODE_1));
		buf[1] = '\0';
		return buf;
	}
	switch (sc)
	{
	case SDL_SCANCODE_0: return "0";
	case SDL_SCANCODE_RETURN: return "Return";
	case SDL_SCANCODE_ESCAPE: return "Escape";
	case SDL_SCANCODE_BACKSPACE: return "Backspace";
	case SDL_SCANCODE_TAB: return "Tab";
	case SDL_SCANCODE_SPACE: return "Space";
	case SDL_SCANCODE_MINUS: return "-";
	case SDL_SCANCODE_EQUALS: return "=";
	case SDL_SCANCODE_LEFTBRACKET: return "[";
	case SDL_SCANCODE_RIGHTBRACKET: return "]";
	case SDL_SCANCODE_BACKSLASH: return "\\";
	case SDL_SCANCODE_NONUSHASH: return "#";
	case SDL_SCANCODE_SEMICOLON: return ";";
	case SDL_SCANCODE_APOSTROPHE: return "'";
	case SDL_SCANCODE_GRAVE: return "`";
	case SDL_SCANCODE_COMMA: return ",";
	case SDL_SCANCODE_PERIOD: return ".";
	case SDL_SCANCODE_SLASH: return "/";
	case SDL_SCANCODE_CAPSLOCK: return "CapsLock";
	case SDL_SCANCODE_F1: return "F1";
	case SDL_SCANCODE_F2: return "F2";
	case SDL_SCANCODE_F3: return "F3";
	case SDL_SCANCODE_F4: return "F4";
	case SDL_SCANCODE_F5: return "F5";
	case SDL_SCANCODE_F6: return "F6";
	case SDL_SCANCODE_F7: return "F7";
	case SDL_SCANCODE_F8: return "F8";
	case SDL_SCANCODE_F9: return "F9";
	case SDL_SCANCODE_F10: return "F10";
	case SDL_SCANCODE_F11: return "F11";
	case SDL_SCANCODE_F12: return "F12";
	case SDL_SCANCODE_PRINTSCREEN: return "PrintScreen";
	case SDL_SCANCODE_SCROLLLOCK: return "ScrollLock";
	case SDL_SCANCODE_PAUSE: return "Pause";
	case SDL_SCANCODE_INSERT: return "Insert";
	case SDL_SCANCODE_HOME: return "Home";
	case SDL_SCANCODE_PAGEUP: return "PageUp";
	case SDL_SCANCODE_DELETE: return "Delete";
	case SDL_SCANCODE_END: return "End";
	case SDL_SCANCODE_PAGEDOWN: return "PageDown";
	case SDL_SCANCODE_RIGHT: return "Right";
	case SDL_SCANCODE_LEFT: return "Left";
	case SDL_SCANCODE_DOWN: return "Down";
	case SDL_SCANCODE_UP: return "Up";
	case SDL_SCANCODE_NUMLOCKCLEAR: return "Numlock";
	case SDL_SCANCODE_KP_DIVIDE: return "Keypad /";
	case SDL_SCANCODE_KP_MULTIPLY: return "Keypad *";
	case SDL_SCANCODE_KP_MINUS: return "Keypad -";
	case SDL_SCANCODE_KP_PLUS: return "Keypad +";
	case SDL_SCANCODE_KP_ENTER: return "Keypad Enter";
	case SDL_SCANCODE_KP_1: return "Keypad 1";
	case SDL_SCANCODE_KP_2: return "Keypad 2";
	case SDL_SCANCODE_KP_3: return "Keypad 3";
	case SDL_SCANCODE_KP_4: return "Keypad 4";
	case SDL_SCANCODE_KP_5: return "Keypad 5";
	case SDL_SCANCODE_KP_6: return "Keypad 6";
	case SDL_SCANCODE_KP_7: return "Keypad 7";
	case SDL_SCANCODE_KP_8: return "Keypad 8";
	case SDL_SCANCODE_KP_9: return "Keypad 9";
	case SDL_SCANCODE_KP_0: return "Keypad 0";
	case SDL_SCANCODE_KP_PERIOD: return "Keypad .";
	case SDL_SCANCODE_KP_EQUALS: return "Keypad =";
	case SDL_SCANCODE_LCTRL: return "Left Ctrl";
	case SDL_SCANCODE_LSHIFT: return "Left Shift";
	case SDL_SCANCODE_LALT: return "Left Alt";
	case SDL_SCANCODE_LGUI: return "Left GUI";
	case SDL_SCANCODE_RCTRL: return "Right Ctrl";
	case SDL_SCANCODE_RSHIFT: return "Right Shift";
	case SDL_SCANCODE_RALT: return "Right Alt";
	case SDL_SCANCODE_RGUI: return "Right GUI";
	default: return "";
	}
}

const char *SDL_GetScancodeName(SDL_Scancode scancode)
{
	/* Single-character names use a shared buffer, as in SDL; copy into a
	 * per-call slot so two names can be used in one statement. */
	static char slots[4][16];
	static int next = 0;
	const char *n = scancode_name((int)scancode);
	char *slot = slots[next];
	next = (next + 1) % 4;
	SDL_strlcpy(slot, n, sizeof(slots[0]));
	return slot;
}

SDL_Scancode SDL_GetScancodeFromName(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return SDL_SCANCODE_UNKNOWN;
	for (int sc = 1; sc < SDL_NUM_SCANCODES; ++sc)
	{
		const char *n = scancode_name(sc);
		if (n[0] != '\0' && strcasecmp(n, name) == 0)
			return (SDL_Scancode)sc;
	}
	return SDL_SCANCODE_UNKNOWN;
}

/* ---- Event queue ---------------------------------------------------------- */

#define QUEUE_SIZE 1024

static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static SDL_Event queue[QUEUE_SIZE];
static int queue_head = 0, queue_tail = 0;
static Uint16 mod_state = KMOD_NONE;

int SDL_PushEvent(SDL_Event *event)
{
	pthread_mutex_lock(&queue_lock);
	int next = (queue_tail + 1) % QUEUE_SIZE;
	int ok = next != queue_head;
	if (ok)
	{
		queue[queue_tail] = *event;
		queue_tail = next;
	}
	pthread_mutex_unlock(&queue_lock);
	return ok ? 1 : SDL_SetError("event queue full");
}

int SDL_PollEvent(SDL_Event *event)
{
	pthread_mutex_lock(&queue_lock);
	int have = queue_head != queue_tail;
	if (have)
	{
		if (event != NULL)
		{
			*event = queue[queue_head];
			queue_head = (queue_head + 1) % QUEUE_SIZE;
		}
	}
	pthread_mutex_unlock(&queue_lock);
	return have;
}

SDL_Keymod SDL_GetModState(void)
{
	pthread_mutex_lock(&queue_lock);
	Uint16 m = mod_state;
	pthread_mutex_unlock(&queue_lock);
	return (SDL_Keymod)m;
}

/* ---- Joysticks: two virtual gamepads fed by the app ------------------------
 *
 * The game counts joysticks once at start-up and has no hot-plugging, so
 * MetalTyrian always reports two gamepads, one per player. The app gives the
 * first connected controller (GameController framework) slot 1 and the
 * second slot 2; an empty slot reads as centred with nothing pressed. Fixed
 * names keep each slot's button assignments whichever controller is in it. */

struct SDL_Joystick { int index; };
static SDL_Joystick pads[MT_PADS] = { { 0 }, { 1 } };

static pthread_mutex_t pad_lock = PTHREAD_MUTEX_INITIALIZER;
static Sint16 pad_axes[MT_PADS][MT_PAD_AXES];
static Uint32 pad_buttons[MT_PADS];
static Uint8 pad_hat[MT_PADS];

void mt_set_gamepad(int pad, const int16_t *axes, uint32_t buttons, uint8_t hat)
{
	if (pad < 0 || pad >= MT_PADS)
		return;
	pthread_mutex_lock(&pad_lock);
	for (int i = 0; i < MT_PAD_AXES; i++)
		pad_axes[pad][i] = axes[i];
	pad_buttons[pad] = buttons;
	pad_hat[pad] = hat;
	pthread_mutex_unlock(&pad_lock);
}

int SDL_NumJoysticks(void) { return MT_PADS; }

SDL_Joystick *SDL_JoystickOpen(int device_index)
{
	if (device_index < 0 || device_index >= MT_PADS)
	{
		SDL_SetError("no such joystick");
		return NULL;
	}
	return &pads[device_index];
}

void SDL_JoystickClose(SDL_Joystick *joystick) { (void)joystick; }

const char *SDL_JoystickName(SDL_Joystick *joystick)
{
	return joystick->index == 0 ? "MetalTyrian Gamepad 1" : "MetalTyrian Gamepad 2";
}

int SDL_JoystickNumAxes(SDL_Joystick *joystick) { (void)joystick; return MT_PAD_AXES; }
int SDL_JoystickNumButtons(SDL_Joystick *joystick) { (void)joystick; return MT_PAD_BUTTONS; }
int SDL_JoystickNumHats(SDL_Joystick *joystick) { (void)joystick; return 1; }

Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis)
{
	if (axis < 0 || axis >= MT_PAD_AXES)
		return 0;
	pthread_mutex_lock(&pad_lock);
	Sint16 v = pad_axes[joystick->index][axis];
	pthread_mutex_unlock(&pad_lock);
	return v;
}

Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button)
{
	if (button < 0 || button >= MT_PAD_BUTTONS)
		return 0;
	pthread_mutex_lock(&pad_lock);
	Uint8 v = (pad_buttons[joystick->index] >> button) & 1;
	pthread_mutex_unlock(&pad_lock);
	return v;
}

Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat)
{
	if (hat != 0)
		return SDL_HAT_CENTERED;
	pthread_mutex_lock(&pad_lock);
	Uint8 v = pad_hat[joystick->index];
	pthread_mutex_unlock(&pad_lock);
	return v;
}

void SDL_JoystickUpdate(void) {}
int SDL_JoystickEventState(int state) { return state == SDL_QUERY ? SDL_IGNORE : state; }

/* ---- Audio: one output device, pulled by the app ---------------------------
 *
 * The game opens a device with a callback that mixes its music (emulated
 * AdLib) and sound effects. Nothing plays here: the app's audio engine calls
 * mt_audio_render() from its real-time thread, which runs the game's callback
 * under the device lock, as SDL does. The rate is always MT_AUDIO_RATE, the
 * rate the game asks for; macOS converts to the hardware rate. */

static pthread_mutex_t audio_lock;
static pthread_once_t audio_lock_once = PTHREAD_ONCE_INIT;
static SDL_AudioSpec audio_spec;
static int audio_open = 0;
static int audio_paused = 1;
static Sint16 *audio_scratch = NULL;
static int audio_scratch_len = 0;

static void audio_lock_init(void)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);  /* SDL's lock nests */
	pthread_mutex_init(&audio_lock, &attr);
	pthread_mutexattr_destroy(&attr);
}

static void audio_lock_take(void)
{
	pthread_once(&audio_lock_once, audio_lock_init);
	pthread_mutex_lock(&audio_lock);
}

static void audio_lock_give(void)
{
	pthread_mutex_unlock(&audio_lock);
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired,
                                      SDL_AudioSpec *obtained, int allowed_changes)
{
	(void)device; (void)allowed_changes;
	if (iscapture)
	{
		SDL_SetError("no recording");
		return 0;
	}
	if (desired->format != AUDIO_S16SYS || (desired->channels != 1 && desired->channels != 2) || desired->callback == NULL)
	{
		SDL_SetError("MetalTyrian audio supports mono or stereo 16-bit with a callback only");
		return 0;
	}
	audio_lock_take();
	if (audio_open)
	{
		audio_lock_give();
		SDL_SetError("audio device already open");
		return 0;
	}
	audio_spec = *desired;
	audio_spec.freq = MT_AUDIO_RATE;
	audio_spec.silence = 0;
	audio_spec.size = audio_spec.samples * audio_spec.channels * sizeof(Sint16);
	audio_open = 1;
	audio_paused = 1;
	audio_lock_give();
	if (obtained)
		*obtained = audio_spec;
	return 1;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
	(void)dev;
	audio_lock_take();
	audio_open = 0;
	audio_paused = 1;
	free(audio_scratch);
	audio_scratch = NULL;
	audio_scratch_len = 0;
	audio_lock_give();
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
	(void)dev;
	audio_lock_take();
	audio_paused = pause_on ? 1 : 0;
	audio_lock_give();
}

void SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; audio_lock_take(); }
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; audio_lock_give(); }

void mt_audio_render(float *left, float *right, int frames)
{
	audio_lock_take();
	if (!audio_open || audio_paused || frames <= 0)
	{
		audio_lock_give();
		if (frames > 0)
		{
			memset(left, 0, (size_t)frames * sizeof(float));
			memset(right, 0, (size_t)frames * sizeof(float));
		}
		return;
	}
	const int ch = audio_spec.channels;
	if (frames * ch > audio_scratch_len)
	{
		/* Grows once or twice at start-up, to the largest buffer macOS asks for. */
		Sint16 *bigger = realloc(audio_scratch, (size_t)(frames * ch) * sizeof(Sint16));
		if (bigger == NULL)
		{
			audio_lock_give();
			memset(left, 0, (size_t)frames * sizeof(float));
			memset(right, 0, (size_t)frames * sizeof(float));
			return;
		}
		audio_scratch = bigger;
		audio_scratch_len = frames * ch;
	}
	audio_spec.callback(audio_spec.userdata, (Uint8 *)audio_scratch, frames * ch * (int)sizeof(Sint16));
	for (int i = 0; i < frames; i++)
	{
		left[i] = audio_scratch[i * ch] * (1.0f / 32768.0f);
		right[i] = audio_scratch[i * ch + ch - 1] * (1.0f / 32768.0f);
	}
	audio_lock_give();
}

/* Sample conversion. The game uses it once, at start-up, to turn its 8-bit
 * 11025 Hz sound effects into 16-bit samples at the output rate. Mono only;
 * the rate change uses a windowed-sinc filter (see SDL_ConvertAudio). */

static int audio_format_bytes(SDL_AudioFormat f)
{
	return (f & 0xFF) / 8;
}

#define MT_SINC_ZEROS 8
#define MT_KAISER_BETA 7.0

static double sinc(double x)
{
	if (fabs(x) < 1e-9)
		return 1.0;
	return sin(M_PI * x) / (M_PI * x);
}

/* Zeroth-order modified Bessel function, for the Kaiser window. */
static double bessel_i0(double x)
{
	double sum = 1.0, term = 1.0;
	for (int k = 1; k < 30; k++)
	{
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
	}
	return sum;
}

/* Kaiser window over t in -1..1. */
static double kaiser(double t)
{
	if (t <= -1.0 || t >= 1.0)
		return 0.0;
	return bessel_i0(MT_KAISER_BETA * sqrt(1.0 - t * t)) / bessel_i0(MT_KAISER_BETA);
}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, SDL_AudioFormat src_format, Uint8 src_channels, int src_rate,
                      SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate)
{
	memset(cvt, 0, sizeof(*cvt));
	if (src_channels != 1 || dst_channels != 1)
		return SDL_SetError("MetalTyrian converts mono audio only");
	if ((src_format != AUDIO_S8 && src_format != AUDIO_U8 && src_format != AUDIO_S16SYS) ||
	    dst_format != AUDIO_S16SYS || src_rate <= 0 || dst_rate <= 0)
		return SDL_SetError("unsupported audio conversion");
	cvt->src_format = src_format;
	cvt->dst_format = dst_format;
	cvt->src_rate = src_rate;
	cvt->dst_rate = dst_rate;
	cvt->rate_incr = (double)dst_rate / src_rate;
	cvt->len_ratio = cvt->rate_incr * audio_format_bytes(dst_format) / audio_format_bytes(src_format);
	/* The buffer must hold the input and the output; round up generously. */
	cvt->len_mult = (int)(cvt->len_ratio + 1.0) + 1;
	cvt->needed = (src_format != dst_format || src_rate != dst_rate) ? 1 : 0;
	return cvt->needed;
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
	if (cvt->buf == NULL)
		return SDL_SetError("no buffer");
	const int in_bytes = audio_format_bytes(cvt->src_format);
	const int n_in = cvt->len / in_bytes;
	if (n_in <= 0)
	{
		cvt->len_cvt = 0;
		return 0;
	}

	/* Read the input into 16-bit samples first, since output overwrites it. */
	Sint16 *in = malloc((size_t)n_in * sizeof(Sint16));
	if (in == NULL)
		return SDL_SetError("out of memory");
	for (int i = 0; i < n_in; i++)
	{
		switch (cvt->src_format)
		{
		case AUDIO_S8:  in[i] = (Sint16)(((Sint8)cvt->buf[i]) * 256); break;
		case AUDIO_U8:  in[i] = (Sint16)((cvt->buf[i] - 128) * 256); break;
		default:        in[i] = ((Sint16 *)cvt->buf)[i]; break;
		}
	}

	/* Same length rule as SDL: n_out = n_in * dst / src, rounded down. The
	 * rate change is a windowed-sinc low-pass (Kaiser window), like SDL's own
	 * resampler, so effects sound as they do in OpenTyrian rather than with
	 * the metallic edge of plain interpolation. */
	const int n_out = (int)((long long)n_in * cvt->dst_rate / cvt->src_rate);
	const double ratio = (double)cvt->src_rate / cvt->dst_rate;   /* input step per output sample */
	const double cutoff = ratio < 1.0 ? 1.0 : 1.0 / ratio;        /* in input-rate terms */
	const int half = (int)ceil(MT_SINC_ZEROS / cutoff);
	Sint16 *out = (Sint16 *)cvt->buf;
	for (int i = 0; i < n_out; i++)
	{
		const double pos = i * ratio;
		const int centre = (int)floor(pos);
		double acc = 0, wsum = 0;
		for (int k = centre - half + 1; k <= centre + half; k++)
		{
			const double x = (pos - k) * cutoff;      /* in zero crossings */
			if (fabs(x) >= MT_SINC_ZEROS)
				continue;
			const double w = sinc(x) * kaiser(x / MT_SINC_ZEROS);
			wsum += w;
			if (k >= 0 && k < n_in)
				acc += in[k] * w;
		}
		const double v = wsum != 0 ? acc / wsum : 0;
		out[i] = (Sint16)lrint(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
	}
	free(in);
	cvt->len_cvt = n_out * (int)sizeof(Sint16);
	return 0;
}

/* ---- Host interface: frames ------------------------------------------------ */

static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
static Uint8 frame_pixels[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT];
static Uint32 frame_palette[256];
static Uint64 frame_number = 0;

/* Text: staged by mt_set_frame_text, moved with the next frame. */
static mt_text_item pending_text[MT_TEXT_MAX], frame_text[MT_TEXT_MAX];
static int pending_text_count = 0, frame_text_count = 0;
static Uint8 pending_mask[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT], frame_mask[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT];

static mt_text_cursor pending_cursor, frame_cursor;
static mt_title_frame pending_title, frame_title;
static uint8_t pending_title_fg[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT], frame_title_fg[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT];

static uint8_t pending_layer[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT], pending_under[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT];
static uint8_t frame_layer[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT], frame_under[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT];
static uint8_t copied_layer[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT], copied_under[MT_SCREEN_WIDTH * MT_SCREEN_HEIGHT];
static int pending_edges = 0, frame_edges = 0, copied_edges = 0;

void mt_set_frame_edges(const uint8_t *layer, const uint8_t *under)
{
	pthread_mutex_lock(&frame_lock);
	pending_edges = layer && under;
	if (pending_edges)
	{
		memcpy(pending_layer, layer, sizeof(pending_layer));
		memcpy(pending_under, under, sizeof(pending_under));
	}
	pthread_mutex_unlock(&frame_lock);
}

int mt_copied_frame_edges(uint8_t *layer, uint8_t *under)
{
	if (copied_edges)
	{
		memcpy(layer, copied_layer, sizeof(copied_layer));
		memcpy(under, copied_under, sizeof(copied_under));
	}
	return copied_edges;
}

void mt_set_frame_title(const mt_title_frame *title, const uint8_t *foreground)
{
	pthread_mutex_lock(&frame_lock);
	if (title)
	{
		pending_title = *title;
		if (foreground)
			memcpy(pending_title_fg, foreground, sizeof(pending_title_fg));
	}
	else
		pending_title.shown = 0;
	pthread_mutex_unlock(&frame_lock);
}

void mt_set_frame_text(const mt_text_item *items, int count, const uint8_t *mask, const mt_text_cursor *cursor)
{
	if (count > MT_TEXT_MAX)
		count = MT_TEXT_MAX;
	pthread_mutex_lock(&frame_lock);
	memcpy(pending_text, items, (size_t)count * sizeof(*items));
	pending_text_count = count;
	if (mask)
		memcpy(pending_mask, mask, sizeof(pending_mask));
	else
		memset(pending_mask, 0, sizeof(pending_mask));
	if (cursor)
		pending_cursor = *cursor;
	else
		pending_cursor.shown = 0;
	pthread_mutex_unlock(&frame_lock);
}

void mt_present_frame(const void *pixels, int pitch, const uint32_t *palette)
{
	pthread_mutex_lock(&frame_lock);
	for (int y = 0; y < MT_SCREEN_HEIGHT; ++y)
		memcpy(frame_pixels + y * MT_SCREEN_WIDTH, (const Uint8 *)pixels + (size_t)y * (size_t)pitch, MT_SCREEN_WIDTH);
	memcpy(frame_palette, palette, sizeof(frame_palette));
	memcpy(frame_text, pending_text, (size_t)pending_text_count * sizeof(*pending_text));
	frame_text_count = pending_text_count;
	memcpy(frame_mask, pending_mask, sizeof(frame_mask));
	pending_text_count = 0;              /* text must be handed over again for the next frame */
	memset(pending_mask, 0, sizeof(pending_mask));
	frame_cursor = pending_cursor;
	pending_cursor.shown = 0;
	frame_title = pending_title;
	if (frame_title.shown)
		memcpy(frame_title_fg, pending_title_fg, sizeof(frame_title_fg));
	pending_title.shown = 0;
	frame_edges = pending_edges;
	if (frame_edges)
	{
		memcpy(frame_layer, pending_layer, sizeof(frame_layer));
		memcpy(frame_under, pending_under, sizeof(frame_under));
	}
	pending_edges = 0;
	++frame_number;
	Uint64 n = frame_number;
	pthread_mutex_unlock(&frame_lock);

	/* Regression testing: MT_DUMP_DIR=<dir> MT_DUMP_EVERY=<n> writes every nth
	 * frame as 64000 index bytes plus 256 little-endian 0x00RRGGBB entries. */
	static const char *dump_dir = NULL;
	static Uint64 dump_every = 0;
	static int dump_checked = 0;
	if (!dump_checked)
	{
		dump_checked = 1;
		dump_dir = getenv("MT_DUMP_DIR");
		const char *every = getenv("MT_DUMP_EVERY");
		dump_every = every ? strtoull(every, NULL, 10) : 50;
	}
	if (dump_dir != NULL && dump_every > 0 && n % dump_every == 0)
	{
		char name[1024];
		snprintf(name, sizeof(name), "%s/f%06llu.raw", dump_dir, (unsigned long long)n);
		FILE *f = fopen(name, "wb");
		if (f != NULL)
		{
			for (int y = 0; y < MT_SCREEN_HEIGHT; ++y)
				fwrite((const Uint8 *)pixels + (size_t)y * (size_t)pitch, 1, MT_SCREEN_WIDTH, f);
			fwrite(palette, 4, 256, f);
			fclose(f);
		}
	}
}

uint64_t mt_copy_frame(uint64_t last_seen, uint8_t *pixels, uint32_t *palette)
{
	pthread_mutex_lock(&frame_lock);
	Uint64 n = frame_number;
	if (n != last_seen && n != 0)
	{
		memcpy(pixels, frame_pixels, sizeof(frame_pixels));
		memcpy(palette, frame_palette, sizeof(frame_palette));
	}
	pthread_mutex_unlock(&frame_lock);
	return n;
}

uint64_t mt_copy_frame_with_text(uint64_t last_seen, uint8_t *pixels, uint32_t *palette,
                                 mt_text_item *items, int max, int *count, uint8_t *mask,
                                 mt_text_cursor *cursor, mt_title_frame *title,
                                 uint8_t *title_foreground)
{
	pthread_mutex_lock(&frame_lock);
	Uint64 n = frame_number;
	if (n != last_seen && n != 0)
	{
		memcpy(pixels, frame_pixels, sizeof(frame_pixels));
		memcpy(palette, frame_palette, sizeof(frame_palette));
		int c = frame_text_count < max ? frame_text_count : max;
		memcpy(items, frame_text, (size_t)c * sizeof(*items));
		*count = c;
		memcpy(mask, frame_mask, sizeof(frame_mask));
		if (cursor)
			*cursor = frame_cursor;
		if (title)
		{
			if (frame_title.shown)
			{
				*title = frame_title;
				if (title_foreground)
					memcpy(title_foreground, frame_title_fg, sizeof(frame_title_fg));
			}
			else
				title->shown = 0;
		}
		copied_edges = frame_edges;
		if (copied_edges)
		{
			memcpy(copied_layer, frame_layer, sizeof(copied_layer));
			memcpy(copied_under, frame_under, sizeof(copied_under));
		}
	}
	pthread_mutex_unlock(&frame_lock);
	return n;
}

/* ---- Host interface: input ------------------------------------------------- */

static Uint16 to_kmod(int mods)
{
	Uint16 m = KMOD_NONE;
	if (mods & MT_MOD_SHIFT) m |= KMOD_LSHIFT;
	if (mods & MT_MOD_CTRL) m |= KMOD_LCTRL;
	if (mods & MT_MOD_ALT) m |= KMOD_LALT;
	if (mods & MT_MOD_GUI) m |= KMOD_LGUI;
	return m;
}

static Uint32 event_time(void) { return SDL_GetTicks(); }

void mt_push_key(int scancode, bool down, int mods)
{
	if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES)
		return;
	pthread_mutex_lock(&queue_lock);
	mod_state = to_kmod(mods);
	pthread_mutex_unlock(&queue_lock);

	SDL_Event e;
	memset(&e, 0, sizeof(e));
	e.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	e.key.timestamp = event_time();
	e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.key.keysym.scancode = (SDL_Scancode)scancode;
	e.key.keysym.mod = to_kmod(mods);
	SDL_PushEvent(&e);
}

void mt_push_text(const char *utf8)
{
	SDL_Event e;
	memset(&e, 0, sizeof(e));
	e.text.type = SDL_TEXTINPUT;
	e.text.timestamp = event_time();
	SDL_strlcpy(e.text.text, utf8, sizeof(e.text.text));
	SDL_PushEvent(&e);
}

void mt_push_mouse_motion(int x, int y, int dx, int dy)
{
	SDL_Event e;
	memset(&e, 0, sizeof(e));
	e.motion.type = SDL_MOUSEMOTION;
	e.motion.timestamp = event_time();
	e.motion.x = x;
	e.motion.y = y;
	e.motion.xrel = dx;
	e.motion.yrel = dy;
	SDL_PushEvent(&e);
}

void mt_push_mouse_button(int button, bool down, int x, int y)
{
	SDL_Event e;
	memset(&e, 0, sizeof(e));
	e.button.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	e.button.timestamp = event_time();
	e.button.button = (Uint8)button;
	e.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	e.button.clicks = 1;
	e.button.x = x;
	e.button.y = y;
	SDL_PushEvent(&e);
}

static char title_version[64] = "Metal";

void mt_set_title_version(const char *text)
{
	if (text)
		snprintf(title_version, sizeof(title_version), "%s", text);
}

const char *mt_title_version(void)
{
	return title_version;
}

void mt_push_focus(bool focused)
{
	SDL_Event e;
	memset(&e, 0, sizeof(e));
	e.window.type = SDL_WINDOWEVENT;
	e.window.timestamp = event_time();
	e.window.event = focused ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
	SDL_PushEvent(&e);
}

void mt_push_quit(void)
{
	SDL_Event e;
	memset(&e, 0, sizeof(e));
	e.type = SDL_QUIT;
	SDL_PushEvent(&e);
}

/* ---- Host interface: the game thread ---------------------------------------- */

/* OpenTyrian's main(), renamed by the build (-Dmain=tyrian_main). */
extern int tyrian_main(int argc, char *argv[]);

static int game_argc;
static char **game_argv;
static volatile int game_done = 0;

static void *game_thread(void *arg)
{
	(void)arg;
	tyrian_main(game_argc, game_argv);
	game_done = 1;
	return NULL;
}

void mt_start_game(int argc, const char *const *argv)
{
	SDL_GetTicks(); /* start the clock */

	game_argc = argc;
	game_argv = calloc((size_t)argc + 1, sizeof(char *));
	for (int i = 0; i < argc; ++i)
		game_argv[i] = strdup(argv[i]);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 16u * 1024u * 1024u);
	pthread_t thread;
	pthread_create(&thread, &attr, game_thread, NULL);
	pthread_attr_destroy(&attr);
	pthread_detach(thread);
}

bool mt_game_finished(void) { return game_done != 0; }
