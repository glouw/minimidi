#pragma once

// THIS HEADER ADDS JUST THE BASIC NECESSITIES
// OF SDL.H TO INCREASE COMPILATION TIMES
//
// SDL.h = 48340 lines
// SDLite.h = 5735 lines

#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_audio.h>

#define SDL_INIT_AUDIO 0x00000010u

extern DECLSPEC int SDLCALL SDL_Init(uint32_t flags);

extern DECLSPEC void SDLCALL SDL_Quit(void);
