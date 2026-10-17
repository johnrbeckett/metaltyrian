/*
 * MetalTyrian SDL shim: just enough of the SDL 2 API for OpenTyrian2000,
 * implemented on top of the MetalTyrian host (Metal, AppKit, and later
 * GameController and AVAudioEngine). Not SDL; no SDL code is included.
 * GPLv2 or later, as the rest of MetalTyrian.
 */
#ifndef MT_SDL_TYPES_H
#define MT_SDL_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef int8_t   Sint8;
typedef uint8_t  Uint8;
typedef int16_t  Sint16;
typedef uint16_t Uint16;
typedef int32_t  Sint32;
typedef uint32_t Uint32;
typedef int64_t  Sint64;
typedef uint64_t Uint64;

typedef enum { SDL_FALSE = 0, SDL_TRUE = 1 } SDL_bool;

#endif
