/*
===========================================================================
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of Spearmint Source Code.

Spearmint Source Code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3 of the License,
or (at your option) any later version.

Spearmint Source Code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Spearmint Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, Spearmint Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following
the terms and conditions of the GNU General Public License.  If not, please
request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional
terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc.,
Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

#ifdef USE_LOCAL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../client/client.h"
#include "../sys/sys_local.h"

static cvar_t *in_keyboardDebug     = NULL;

static SDL_Joystick *stick[MAX_SPLITVIEW] = {NULL, NULL, NULL, NULL};

static qboolean mouseAvailable = qfalse;
static qboolean mouseActive = qfalse;

static cvar_t *in_mouse             = NULL;
static cvar_t *in_nograb;

static cvar_t *in_joystick[MAX_SPLITVIEW] 			= {NULL, NULL, NULL, NULL};
static cvar_t *in_joystickDebug[MAX_SPLITVIEW]		= {NULL, NULL, NULL, NULL};
static cvar_t *in_joystickThreshold[MAX_SPLITVIEW]	= {NULL, NULL, NULL, NULL};
static cvar_t *in_joystickNo[MAX_SPLITVIEW]			= {NULL, NULL, NULL, NULL};
static cvar_t *in_joystickUseAnalog[MAX_SPLITVIEW]	= {NULL, NULL, NULL, NULL};

static int vidRestartTime = 0;

static SDL_Window *SDL_window = NULL;

#define CTRL(a) ((a)-'a'+1)

/*
===============
IN_PrintKey
===============
*/
static void IN_PrintKey( const SDL_Keysym *keysym, keyNum_t key, qboolean down )
{
	if( down )
		Com_Printf( "+ " );
	else
		Com_Printf( "  " );

	Com_Printf( "Scancode: 0x%02x(%s) Sym: 0x%02x(%s)",
			keysym->scancode, SDL_GetScancodeName( keysym->scancode ),
			keysym->sym, SDL_GetKeyName( keysym->sym ) );

	if( keysym->mod & KMOD_LSHIFT )   Com_Printf( " KMOD_LSHIFT" );
	if( keysym->mod & KMOD_RSHIFT )   Com_Printf( " KMOD_RSHIFT" );
	if( keysym->mod & KMOD_LCTRL )    Com_Printf( " KMOD_LCTRL" );
	if( keysym->mod & KMOD_RCTRL )    Com_Printf( " KMOD_RCTRL" );
	if( keysym->mod & KMOD_LALT )     Com_Printf( " KMOD_LALT" );
	if( keysym->mod & KMOD_RALT )     Com_Printf( " KMOD_RALT" );
	if( keysym->mod & KMOD_LGUI )     Com_Printf( " KMOD_LGUI" );
	if( keysym->mod & KMOD_RGUI )     Com_Printf( " KMOD_RGUI" );
	if( keysym->mod & KMOD_NUM )      Com_Printf( " KMOD_NUM" );
	if( keysym->mod & KMOD_CAPS )     Com_Printf( " KMOD_CAPS" );
	if( keysym->mod & KMOD_MODE )     Com_Printf( " KMOD_MODE" );
	if( keysym->mod & KMOD_RESERVED ) Com_Printf( " KMOD_RESERVED" );

	Com_Printf( " Q:0x%02x(%s)\n", key, Key_KeynumToString( key ) );
}

#define MAX_CONSOLE_KEYS 16

/*
===============
IN_IsConsoleKey

TODO: If the SDL_Scancode situation improves, use it instead of
      both of these methods
===============
*/
static qboolean IN_IsConsoleKey( keyNum_t key, int character )
{
	typedef struct consoleKey_s
	{
		enum
		{
			QUAKE_KEY,
			CHARACTER
		} type;

		union
		{
			keyNum_t key;
			int character;
		} u;
	} consoleKey_t;

	static consoleKey_t consoleKeys[ MAX_CONSOLE_KEYS ];
	static int numConsoleKeys = 0;
	int i;

	// Only parse the variable when it changes
	if( cl_consoleKeys->modified )
	{
		char *text_p, *token;

		cl_consoleKeys->modified = qfalse;
		text_p = cl_consoleKeys->string;
		numConsoleKeys = 0;

		while( numConsoleKeys < MAX_CONSOLE_KEYS )
		{
			consoleKey_t *c = &consoleKeys[ numConsoleKeys ];
			int charCode = 0;

			token = COM_Parse( &text_p );
			if( !token[ 0 ] )
				break;

			if( strlen( token ) == 4 )
				charCode = Com_HexStrToInt( token );

			if( charCode > 0 )
			{
				c->type = CHARACTER;
				c->u.character = charCode;
			}
			else
			{
				c->type = QUAKE_KEY;
				c->u.key = Key_StringToKeynum( token );

				// 0 isn't a key
				if( c->u.key <= 0 )
					continue;
			}

			numConsoleKeys++;
		}
	}

	// If the character is the same as the key, prefer the character
	if( key == character )
		key = 0;

	for( i = 0; i < numConsoleKeys; i++ )
	{
		consoleKey_t *c = &consoleKeys[ i ];

		switch( c->type )
		{
			case QUAKE_KEY:
				if( key && c->u.key == key )
					return qtrue;
				break;

			case CHARACTER:
				if( c->u.character == character )
					return qtrue;
				break;
		}
	}

	return qfalse;
}

/*
===============
IN_TranslateSDLToQ3Key
===============
*/
static keyNum_t IN_TranslateSDLToQ3Key( SDL_Keysym *keysym, qboolean down )
{
	keyNum_t key = 0;

	if( keysym->sym >= SDLK_SPACE && keysym->sym < SDLK_DELETE )
	{
		// These happen to match the ASCII chars
		key = (int)keysym->sym;
	}
	else
	{
		switch( keysym->sym )
		{
			case SDLK_PAGEUP:       key = K_PGUP;          break;
			case SDLK_KP_9:         key = K_KP_PGUP;       break;
			case SDLK_PAGEDOWN:     key = K_PGDN;          break;
			case SDLK_KP_3:         key = K_KP_PGDN;       break;
			case SDLK_KP_7:         key = K_KP_HOME;       break;
			case SDLK_HOME:         key = K_HOME;          break;
			case SDLK_KP_1:         key = K_KP_END;        break;
			case SDLK_END:          key = K_END;           break;
			case SDLK_KP_4:         key = K_KP_LEFTARROW;  break;
			case SDLK_LEFT:         key = K_LEFTARROW;     break;
			case SDLK_KP_6:         key = K_KP_RIGHTARROW; break;
			case SDLK_RIGHT:        key = K_RIGHTARROW;    break;
			case SDLK_KP_2:         key = K_KP_DOWNARROW;  break;
			case SDLK_DOWN:         key = K_DOWNARROW;     break;
			case SDLK_KP_8:         key = K_KP_UPARROW;    break;
			case SDLK_UP:           key = K_UPARROW;       break;
			case SDLK_ESCAPE:       key = K_ESCAPE;        break;
			case SDLK_KP_ENTER:     key = K_KP_ENTER;      break;
			case SDLK_RETURN:       key = K_ENTER;         break;
			case SDLK_TAB:          key = K_TAB;           break;
			case SDLK_F1:           key = K_F1;            break;
			case SDLK_F2:           key = K_F2;            break;
			case SDLK_F3:           key = K_F3;            break;
			case SDLK_F4:           key = K_F4;            break;
			case SDLK_F5:           key = K_F5;            break;
			case SDLK_F6:           key = K_F6;            break;
			case SDLK_F7:           key = K_F7;            break;
			case SDLK_F8:           key = K_F8;            break;
			case SDLK_F9:           key = K_F9;            break;
			case SDLK_F10:          key = K_F10;           break;
			case SDLK_F11:          key = K_F11;           break;
			case SDLK_F12:          key = K_F12;           break;
			case SDLK_F13:          key = K_F13;           break;
			case SDLK_F14:          key = K_F14;           break;
			case SDLK_F15:          key = K_F15;           break;

			case SDLK_BACKSPACE:    key = K_BACKSPACE;     break;
			case SDLK_KP_PERIOD:    key = K_KP_DEL;        break;
			case SDLK_DELETE:       key = K_DEL;           break;
			case SDLK_PAUSE:        key = K_PAUSE;         break;

			case SDLK_LSHIFT:
			case SDLK_RSHIFT:       key = K_SHIFT;         break;

			case SDLK_LCTRL:
			case SDLK_RCTRL:        key = K_CTRL;          break;

			case SDLK_RGUI:
			case SDLK_LGUI:         key = K_COMMAND;       break;

			case SDLK_RALT:
			case SDLK_LALT:         key = K_ALT;           break;

			case SDLK_KP_5:         key = K_KP_5;          break;
			case SDLK_INSERT:       key = K_INS;           break;
			case SDLK_KP_0:         key = K_KP_INS;        break;
			case SDLK_KP_MULTIPLY:  key = K_KP_STAR;       break;
			case SDLK_KP_PLUS:      key = K_KP_PLUS;       break;
			case SDLK_KP_MINUS:     key = K_KP_MINUS;      break;
			case SDLK_KP_DIVIDE:    key = K_KP_SLASH;      break;

			case SDLK_MODE:         key = K_MODE;          break;
			case SDLK_HELP:         key = K_HELP;          break;
			case SDLK_PRINTSCREEN:  key = K_PRINT;         break;
			case SDLK_SYSREQ:       key = K_SYSREQ;        break;
			case SDLK_MENU:         key = K_MENU;          break;
			case SDLK_POWER:        key = K_POWER;         break;
			case SDLK_UNDO:         key = K_UNDO;          break;
			case SDLK_SCROLLLOCK:   key = K_SCROLLOCK;     break;
			case SDLK_CAPSLOCK:     key = K_CAPSLOCK;      break;

			default:
				break;
		}
	}

	if( in_keyboardDebug->integer )
		IN_PrintKey( keysym, key, down );

	if( IN_IsConsoleKey( key, 0 ) )
	{
		// Console keys can't be bound or generate characters
		key = K_CONSOLE;
	}

	return key;
}

/*
===============
IN_GobbleMotionEvents
===============
*/
static void IN_GobbleMotionEvents( void )
{
	SDL_Event dummy[ 1 ];

	// Gobble any mouse motion events
	SDL_PumpEvents( );
	while( SDL_PeepEvents( dummy, 1, SDL_GETEVENT,
		SDL_MOUSEMOTION, SDL_MOUSEMOTION ) ) { }
}

/*
===============
IN_GetUIMousePosition
===============
*/
static void IN_GetUIMousePosition( int localClientNum, int *x, int *y )
{
	if( uivm )
	{
		int pos = VM_Call( uivm, UI_MOUSE_POSITION, localClientNum );
		*x = Com_Clamp(0, cls.glconfig.vidWidth - 1, pos & 0xFFFF);
		*y = Com_Clamp(0, cls.glconfig.vidHeight - 1, ( pos >> 16 ) & 0xFFFF);
	}
	else
	{
		*x = cls.glconfig.vidWidth / 2;
		*y = cls.glconfig.vidHeight / 2;
	}
}

/*
===============
IN_SetUIMousePosition
===============
*/
static void IN_SetUIMousePosition( int localClientNum, int x, int y )
{
	if( uivm )
	{
		VM_Call( uivm, UI_SET_MOUSE_POSITION, localClientNum, x, y );
	}
}

/*
===============
IN_ActivateMouse
===============
*/
static void IN_ActivateMouse( void )
{
	if (!mouseAvailable || !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	if( !mouseActive )
	{
		SDL_SetRelativeMouseMode( SDL_TRUE );
		SDL_SetWindowGrab( SDL_window, 1 );

		IN_GobbleMotionEvents( );
	}

	// in_nograb makes no sense in fullscreen mode
	if( !Cvar_VariableIntegerValue("r_fullscreen") )
	{
		if( in_nograb->modified || !mouseActive )
		{
			if( in_nograb->integer )
				SDL_SetWindowGrab( SDL_window, 0 );
			else
				SDL_SetWindowGrab( SDL_window, 1 );

			in_nograb->modified = qfalse;
		}
	}

	mouseActive = qtrue;
}

/*
===============
IN_DeactivateMouse
===============
*/
static void IN_DeactivateMouse( qboolean showSystemCursor )
{
	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	// Show the cursor when the mouse is disabled and not drawing UI cursor,
	// but not when fullscreen
	if( !Cvar_VariableIntegerValue("r_fullscreen") )
		SDL_ShowCursor( showSystemCursor );

	if( !mouseAvailable )
		return;

	if( mouseActive )
	{
		IN_GobbleMotionEvents( );

		SDL_SetWindowGrab( SDL_window, 0 );
		SDL_SetRelativeMouseMode( SDL_FALSE );

		// Don't warp the mouse unless the cursor is within the window
		if( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_MOUSE_FOCUS )
		{
			int x, y;
			IN_GetUIMousePosition( 0, &x, &y );
			SDL_WarpMouseInWindow( SDL_window, x, y );
		}

		mouseActive = qfalse;
	}
}

static int joyKeyStart[CL_MAX_SPLITVIEW] = {
	K_JOY1,
#if CL_MAX_SPLITVIEW > 1
	K_2JOY1,
#endif
#if CL_MAX_SPLITVIEW > 2
	K_3JOY1,
#endif
#if CL_MAX_SPLITVIEW > 3
	K_4JOY1
#endif
	};

// We translate axes movement into keypresses
static int joy_keys[CL_MAX_SPLITVIEW][16] = {
	// Client 1
	{
		K_LEFTARROW, K_RIGHTARROW,
		K_UPARROW, K_DOWNARROW,
		K_JOY17, K_JOY18,
		K_JOY19, K_JOY20,
		K_JOY21, K_JOY22,
		K_JOY23, K_JOY24,
		K_JOY25, K_JOY26,
		K_JOY27, K_JOY28
	},
#if CL_MAX_SPLITVIEW > 1
	// Client 2
	{
		K_2JOY17, K_2JOY18,
		K_2JOY19, K_2JOY20,
		K_2JOY21, K_2JOY22,
		K_2JOY23, K_2JOY24,
		K_2JOY25, K_2JOY26,
		K_2JOY27, K_2JOY28,
		K_2JOY29, K_2JOY30,
		K_2JOY31, K_2JOY32
	},
#endif
#if CL_MAX_SPLITVIEW > 2
	// Client 3
	{
		K_3JOY17, K_3JOY18,
		K_3JOY19, K_3JOY20,
		K_3JOY21, K_3JOY22,
		K_3JOY23, K_3JOY24,
		K_3JOY25, K_3JOY26,
		K_3JOY27, K_3JOY28,
		K_3JOY29, K_3JOY30,
		K_3JOY31, K_3JOY32
	},
#endif
#if CL_MAX_SPLITVIEW > 3
	// Client 4
	{
		K_4JOY17, K_4JOY18,
		K_4JOY19, K_4JOY20,
		K_4JOY21, K_4JOY22,
		K_4JOY23, K_4JOY24,
		K_4JOY25, K_4JOY26,
		K_4JOY27, K_4JOY28,
		K_4JOY29, K_4JOY30,
		K_4JOY31, K_4JOY32
	}
#endif
};

// translate hat events into keypresses
// the 4 highest buttons are used for the first hat ...
static int hat_keys[CL_MAX_SPLITVIEW][16] = {
	// Client 1
	{
		K_JOY29, K_JOY30,
		K_JOY31, K_JOY32,
		K_JOY25, K_JOY26,
		K_JOY27, K_JOY28,
		K_JOY21, K_JOY22,
		K_JOY23, K_JOY24,
		K_JOY17, K_JOY18,
		K_JOY19, K_JOY20
	},
#if CL_MAX_SPLITVIEW > 1
	// Client 2
	{
		K_2JOY29, K_2JOY30,
		K_2JOY31, K_2JOY32,
		K_2JOY25, K_2JOY26,
		K_2JOY27, K_2JOY28,
		K_2JOY21, K_2JOY22,
		K_2JOY23, K_2JOY24,
		K_2JOY17, K_2JOY18,
		K_2JOY19, K_2JOY20
	},
#endif
#if CL_MAX_SPLITVIEW > 2
	// Client 3
	{
		K_3JOY29, K_3JOY30,
		K_3JOY31, K_3JOY32,
		K_3JOY25, K_3JOY26,
		K_3JOY27, K_3JOY28,
		K_3JOY21, K_3JOY22,
		K_3JOY23, K_3JOY24,
		K_3JOY17, K_3JOY18,
		K_3JOY19, K_3JOY20
	},
#endif
#if CL_MAX_SPLITVIEW > 3
	// Client 4
	{
		K_4JOY29, K_4JOY30,
		K_4JOY31, K_4JOY32,
		K_4JOY25, K_4JOY26,
		K_4JOY27, K_4JOY28,
		K_4JOY21, K_4JOY22,
		K_4JOY23, K_4JOY24,
		K_4JOY17, K_4JOY18,
		K_4JOY19, K_4JOY20
	}
#endif
};


struct
{
	qboolean buttons[16];  // !!! FIXME: these might be too many.
	unsigned int oldaxes;
	int oldaaxes[MAX_JOYSTICK_AXIS];
	unsigned int oldhats;
} stick_state[CL_MAX_SPLITVIEW];


/*
===============
IN_InitJoystick
===============
*/
static void IN_InitJoystick( void )
{
	int i = 0;
	int total = 0;
	char buf[16384] = "";
	qboolean joyEnabled = qfalse;

	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		if (stick[i] != NULL)
			SDL_JoystickClose(stick[i]);

		stick[i] = NULL;
		memset(&stick_state[i], '\0', sizeof (stick_state[i]));

		if( in_joystick[i]->integer ) {
			joyEnabled = qtrue;
		}
	}

	if (!SDL_WasInit(SDL_INIT_JOYSTICK))
	{
		Com_DPrintf("Calling SDL_Init(SDL_INIT_JOYSTICK)...\n");
		if (SDL_Init(SDL_INIT_JOYSTICK) == -1)
		{
			Com_DPrintf("SDL_Init(SDL_INIT_JOYSTICK) failed: %s\n", SDL_GetError());
			return;
		}
		Com_DPrintf("SDL_Init(SDL_INIT_JOYSTICK) passed.\n");
	}

	total = SDL_NumJoysticks();
	Com_DPrintf("%d possible joysticks\n", total);

	// Print list and build cvar to allow ui to select joystick.
	for (i = 0; i < total; i++)
	{
		Q_strcat(buf, sizeof(buf), SDL_JoystickNameForIndex(i));
		Q_strcat(buf, sizeof(buf), "\n");
	}

	Cvar_Get( "in_availableJoysticks", buf, CVAR_ROM );

	if ( !joyEnabled ) {
		Com_DPrintf( "Joystick is not active.\n" );
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
		return;
	}

	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		if( !in_joystick[i]->integer ) {
			continue;
		}

		in_joystickNo[i] = Cvar_Get( Com_LocalClientCvarName(i, "in_joystickNo"), "0", CVAR_ARCHIVE );
		if( in_joystickNo[i]->integer < 0 || in_joystickNo[i]->integer >= total )
			Cvar_Set( Com_LocalClientCvarName(i, "in_joystickNo"), "0" );

		in_joystickUseAnalog[i] = Cvar_Get( Com_LocalClientCvarName(i, "in_joystickUseAnalog"), "0", CVAR_ARCHIVE );

		stick[i] = SDL_JoystickOpen( in_joystickNo[i]->integer );

		if (stick[i] == NULL) {
			Com_DPrintf( "No joystick opened.\n" );
			return;
		}

		Com_DPrintf( "Joystick %d opened for player %d\n", in_joystickNo[i]->integer, i+1 );
		Com_DPrintf( "Name:       %s\n", SDL_JoystickName(in_joystickNo[i]->integer) );
		Com_DPrintf( "Axes:       %d\n", SDL_JoystickNumAxes(stick[i]) );
		Com_DPrintf( "Hats:       %d\n", SDL_JoystickNumHats(stick[i]) );
		Com_DPrintf( "Buttons:    %d\n", SDL_JoystickNumButtons(stick[i]) );
		Com_DPrintf( "Balls:      %d\n", SDL_JoystickNumBalls(stick[i]) );
		Com_DPrintf( "Use Analog: %s\n", in_joystickUseAnalog[i]->integer ? "Yes" : "No" );
	}

	SDL_JoystickEventState(SDL_QUERY);
}

/*
===============
IN_ShutdownJoystick
===============
*/
static void IN_ShutdownJoystick( void )
{
	int		i;

	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		if (stick[i]) {
			SDL_JoystickClose(stick[i]);
			stick[i] = NULL;
		}
	}

	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

/*
===============
IN_JoyMove
===============
*/
static void IN_JoyMove( void )
{
	qboolean joy_pressed[ARRAY_LEN(joy_keys[0])];
	unsigned int axes = 0;
	unsigned int hats = 0;
	int total = 0;
	int i = 0;
	int joy;

	for (joy = 0; joy < CL_MAX_SPLITVIEW; joy++) {
		if (stick[joy]) {
			break;
		}
	}

	if (joy == CL_MAX_SPLITVIEW) {
		return;
	}

	SDL_JoystickUpdate();

	memset(joy_pressed, '\0', sizeof (joy_pressed));

	for (joy = 0; joy < CL_MAX_SPLITVIEW; joy++) {
		// update the ball state.
		total = SDL_JoystickNumBalls(stick[joy]);
		if (total > 0)
		{
			int balldx = 0;
			int balldy = 0;
			for (i = 0; i < total; i++)
			{
				int dx = 0;
				int dy = 0;
				SDL_JoystickGetBall(stick[joy], i, &dx, &dy);
				balldx += dx;
				balldy += dy;
			}
			if (balldx || balldy)
			{
				// !!! FIXME: is this good for stick balls, or just mice?
				// Scale like the mouse input...
				if (abs(balldx) > 1)
					balldx *= 2;
				if (abs(balldy) > 1)
					balldy *= 2;
				Com_QueueEvent( 0, SE_MOUSE + joy, balldx, balldy, 0, NULL );
			}
		}

		// now query the stick buttons...
		total = SDL_JoystickNumButtons(stick[joy]);
		if (total > 0)
		{
			if (total > ARRAY_LEN(stick_state[joy].buttons))
				total = ARRAY_LEN(stick_state[joy].buttons);
			for (i = 0; i < total; i++)
			{
				qboolean pressed = (SDL_JoystickGetButton(stick[joy], i) != 0);
				if (pressed != stick_state[joy].buttons[i])
				{
					Com_QueueEvent( 0, SE_KEY, joyKeyStart[joy] + i, pressed, 0, NULL );
					stick_state[joy].buttons[i] = pressed;
				}
			}
		}

		// look at the hats...
		total = SDL_JoystickNumHats(stick[joy]);
		if (total > 0)
		{
			if (total > 4) total = 4;
			for (i = 0; i < total; i++)
			{
				((Uint8 *)&hats)[i] = SDL_JoystickGetHat(stick[joy], i);
			}
		}

		// update hat state
		if (hats != stick_state[joy].oldhats)
		{
			for( i = 0; i < 4; i++ ) {
				if( ((Uint8 *)&hats)[i] != ((Uint8 *)&stick_state[joy].oldhats)[i] )
				{
					// release event
					switch( ((Uint8 *)&stick_state[joy].oldhats)[i] )
					{
						case SDL_HAT_UP:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 0], qfalse, 0, NULL );
							break;
						case SDL_HAT_RIGHT:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 1], qfalse, 0, NULL );
							break;
						case SDL_HAT_DOWN:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 2], qfalse, 0, NULL );
							break;
						case SDL_HAT_LEFT:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 3], qfalse, 0, NULL );
							break;
						case SDL_HAT_RIGHTUP:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 0], qfalse, 0, NULL );
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 1], qfalse, 0, NULL );
							break;
						case SDL_HAT_RIGHTDOWN:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 2], qfalse, 0, NULL );
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 1], qfalse, 0, NULL );
							break;
						case SDL_HAT_LEFTUP:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 0], qfalse, 0, NULL );
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 3], qfalse, 0, NULL );
							break;
						case SDL_HAT_LEFTDOWN:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 2], qfalse, 0, NULL );
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 3], qfalse, 0, NULL );
							break;
						default:
							break;
					}
					// press event
					switch( ((Uint8 *)&hats)[i] ) {
						case SDL_HAT_UP:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 0], qtrue, 0, NULL );
							break;
						case SDL_HAT_RIGHT:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 1], qtrue, 0, NULL );
							break;
						case SDL_HAT_DOWN:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 2], qtrue, 0, NULL );
							break;
						case SDL_HAT_LEFT:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 3], qtrue, 0, NULL );
							break;
						case SDL_HAT_RIGHTUP:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 0], qtrue, 0, NULL );
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 1], qtrue, 0, NULL );
							break;
						case SDL_HAT_RIGHTDOWN:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 2], qtrue, 0, NULL );
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 1], qtrue, 0, NULL );
							break;
						case SDL_HAT_LEFTUP:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 0], qtrue, 0, NULL );
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 3], qtrue, 0, NULL );
							break;
						case SDL_HAT_LEFTDOWN:
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 2], qtrue, 0, NULL );
							Com_QueueEvent( 0, SE_KEY, hat_keys[joy][4*i + 3], qtrue, 0, NULL );
							break;
						default:
							break;
					}
				}
			}
		}

		// save hat state
		stick_state[joy].oldhats = hats;

		// finally, look at the axes...
		total = SDL_JoystickNumAxes(stick[joy]);
		if (total > 0)
		{
			if (in_joystickUseAnalog[joy]->integer)
			{
				if (total > MAX_JOYSTICK_AXIS) total = MAX_JOYSTICK_AXIS;
				for (i = 0; i < total; i++)
				{
					Sint16 axis = SDL_JoystickGetAxis(stick[joy], i);
					float f = ( (float) abs(axis) ) / 32767.0f;
					
					if( f < in_joystickThreshold[joy]->value ) axis = 0;

					if ( axis != stick_state[joy].oldaaxes[i] )
					{
						Com_QueueEvent( 0, SE_JOYSTICK_AXIS + joy, i, axis, 0, NULL );
						stick_state[joy].oldaaxes[i] = axis;
					}
				}
			}
			else
			{
				if (total > 16) total = 16;
				for (i = 0; i < total; i++)
				{
					Sint16 axis = SDL_JoystickGetAxis(stick[joy], i);
					float f = ( (float) axis ) / 32767.0f;
					if( f < -in_joystickThreshold[joy]->value ) {
						axes |= ( 1 << ( i * 2 ) );
					} else if( f > in_joystickThreshold[joy]->value ) {
						axes |= ( 1 << ( ( i * 2 ) + 1 ) );
					}
				}
			}
		}

		/* Time to update axes state based on old vs. new. */
		if (axes != stick_state[joy].oldaxes)
		{
			for( i = 0; i < 16; i++ ) {
				if( ( axes & ( 1 << i ) ) && !( stick_state[joy].oldaxes & ( 1 << i ) ) ) {
					Com_QueueEvent( 0, SE_KEY, joy_keys[joy][i], qtrue, 0, NULL );
				}

				if( !( axes & ( 1 << i ) ) && ( stick_state[joy].oldaxes & ( 1 << i ) ) ) {
					Com_QueueEvent( 0, SE_KEY, joy_keys[joy][i], qfalse, 0, NULL );
				}
			}
		}

		/* Save for future generations. */
		stick_state[joy].oldaxes = axes;
	}
}

/*
===============
IN_ProcessEvents
===============
*/
static void IN_ProcessEvents( void )
{
	SDL_Event e;
	keyNum_t key = 0;
	static keyNum_t lastKeyDown = 0;

	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
			return;

	while( SDL_PollEvent( &e ) )
	{
		switch( e.type )
		{
			case SDL_KEYDOWN:
				if( ( key = IN_TranslateSDLToQ3Key( &e.key.keysym, qtrue ) ) )
					Com_QueueEvent( 0, SE_KEY, key, qtrue, 0, NULL );

				lastKeyDown = key;
				break;

			case SDL_KEYUP:
				if( ( key = IN_TranslateSDLToQ3Key( &e.key.keysym, qfalse ) ) )
					Com_QueueEvent( 0, SE_KEY, key, qfalse, 0, NULL );

				lastKeyDown = 0;
				break;

			case SDL_TEXTINPUT:
				if( lastKeyDown != K_CONSOLE )
				{
					char *c = e.text.text;

					// Quick and dirty UTF-8 to UTF-32 conversion
					while( *c )
					{
						int utf32 = 0;

						if( ( *c & 0x80 ) == 0 )
							utf32 = *c++;
						else if( ( *c & 0xE0 ) == 0xC0 ) // 110x xxxx
						{
							utf32 |= ( *c++ & 0x1F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else if( ( *c & 0xF0 ) == 0xE0 ) // 1110 xxxx
						{
							utf32 |= ( *c++ & 0x0F ) << 12;
							utf32 |= ( *c++ & 0x3F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else if( ( *c & 0xF8 ) == 0xF0 ) // 1111 0xxx
						{
							utf32 |= ( *c++ & 0x07 ) << 18;
							utf32 |= ( *c++ & 0x3F ) << 6;
							utf32 |= ( *c++ & 0x3F ) << 6;
							utf32 |= ( *c++ & 0x3F );
						}
						else
						{
							Com_DPrintf( "Unrecognised UTF-8 lead byte: 0x%x\n", (unsigned int)*c );
							c++;
						}

						if( utf32 != 0 )
						{
							if( IN_IsConsoleKey( 0, utf32 ) )
							{
								Com_QueueEvent( 0, SE_KEY, K_CONSOLE, qtrue, 0, NULL );
								Com_QueueEvent( 0, SE_KEY, K_CONSOLE, qfalse, 0, NULL );
							}
							else
								Com_QueueEvent( 0, SE_CHAR, utf32, 0, 0, NULL );
						}
          }
        }
				break;

			case SDL_MOUSEMOTION:
				if( mouseActive )
					Com_QueueEvent( 0, SE_MOUSE, e.motion.xrel, e.motion.yrel, 0, NULL );
				break;

			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				{
					unsigned char b;
					switch( e.button.button )
					{
						case 1:   b = K_MOUSE1;     break;
						case 2:   b = K_MOUSE3;     break;
						case 3:   b = K_MOUSE2;     break;
						case 4:   b = K_MOUSE4;     break;
						case 5:   b = K_MOUSE5;     break;
						default:  b = K_AUX1 + ( e.button.button - 8 ) % 16; break;
					}
					Com_QueueEvent( 0, SE_KEY, b,
						( e.type == SDL_MOUSEBUTTONDOWN ? qtrue : qfalse ), 0, NULL );
				}
				break;

			case SDL_MOUSEWHEEL:
				if( e.wheel.y > 0 )
					Com_QueueEvent( 0, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				else
					Com_QueueEvent( 0, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				break;

			case SDL_QUIT:
				Cbuf_ExecuteText(EXEC_NOW, "quit Closed window\n");
				break;

			case SDL_WINDOWEVENT:
				switch( e.window.event )
				{
					case SDL_WINDOWEVENT_RESIZED:
						{
							char width[32], height[32];
							Com_sprintf( width, sizeof( width ), "%d", e.window.data1 );
							Com_sprintf( height, sizeof( height ), "%d", e.window.data2 );
							Cvar_Set( "r_customwidth", width );
							Cvar_Set( "r_customheight", height );
							Cvar_Set( "r_mode", "-1" );

							// Wait until user stops dragging for 1 second, so
							// we aren't constantly recreating the GL context while
							// he tries to drag...
							vidRestartTime = Sys_Milliseconds( ) + 1000;
						}
						break;

					case SDL_WINDOWEVENT_MINIMIZED:    Cvar_SetValue( "com_minimized", 1 ); break;
					case SDL_WINDOWEVENT_MAXIMIZED:    Cvar_SetValue( "com_minimized", 0 ); break;
					case SDL_WINDOWEVENT_FOCUS_LOST:   Cvar_SetValue( "com_unfocused", 1 ); break;
					case SDL_WINDOWEVENT_FOCUS_GAINED: Cvar_SetValue( "com_unfocused", 0 ); break;
				}
				break;

			default:
				break;
		}
	}
}

/*
===============
IN_Frame
===============
*/
void IN_Frame( void )
{
	qboolean loading;
	qboolean cursorShowing;
	int x, y;

	IN_JoyMove( );
	IN_ProcessEvents( );

	// If not DISCONNECTED (main menu) or ACTIVE (in game), we're loading
	loading = ( clc.state != CA_DISCONNECTED && clc.state != CA_ACTIVE );
	cursorShowing = Key_GetCatcher() & KEYCATCH_UI;

	if( !cls.glconfig.isFullscreen && ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) )
	{
		// Console is down in windowed mode
		IN_DeactivateMouse( qtrue );
	}
	else if( !cls.glconfig.isFullscreen && loading )
	{
		// Loading in windowed mode
		IN_DeactivateMouse( qtrue );
	}
	else if( !Cvar_VariableIntegerValue("r_fullscreen") && cursorShowing )
	{
		// Showing cursor in windowed mode
		IN_DeactivateMouse( qfalse );
	}
	else if( !( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_INPUT_FOCUS ) )
	{
		// Window not got focus
		IN_DeactivateMouse( qtrue );
	}
	else
		IN_ActivateMouse( );

	if( !mouseActive )
	{
		SDL_GetMouseState( &x, &y );
		IN_SetUIMousePosition( 0, x, y );
	}

	// In case we had to delay actual restart of video system
	if( ( vidRestartTime != 0 ) && ( vidRestartTime < Sys_Milliseconds( ) ) )
	{
		vidRestartTime = 0;
		Cbuf_AddText( "vid_restart" );
	}
}

/*
===============
IN_InitKeyLockStates
===============
*/
void IN_InitKeyLockStates( void )
{
	unsigned char *keystate = SDL_GetKeyboardState(NULL);

	keys[K_SCROLLOCK].down = keystate[SDL_SCANCODE_SCROLLLOCK];
	keys[K_KP_NUMLOCK].down = keystate[SDL_SCANCODE_NUMLOCKCLEAR];
	keys[K_CAPSLOCK].down = keystate[SDL_SCANCODE_CAPSLOCK];
}

/*
===============
IN_Init
===============
*/
void IN_Init( void *windowData )
{
	int appState;
	int i;

	if( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		Com_Error( ERR_FATAL, "IN_Init called before SDL_Init( SDL_INIT_VIDEO )" );
		return;
	}

	SDL_window = (SDL_Window *)windowData;

	Com_DPrintf( "\n------- Input Initialization -------\n" );

	in_keyboardDebug = Cvar_Get( "in_keyboardDebug", "0", CVAR_ARCHIVE );

	// mouse variables
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
	in_nograb = Cvar_Get( "in_nograb", "0", CVAR_ARCHIVE );

	for (i = 0; i < CL_MAX_SPLITVIEW; i++) {
		in_joystick[i] = Cvar_Get( Com_LocalClientCvarName(i, "in_joystick"), "0", CVAR_ARCHIVE|CVAR_LATCH );
		in_joystickDebug[i] = Cvar_Get( Com_LocalClientCvarName(i, "in_joystickDebug"), "0", CVAR_TEMP );
		in_joystickThreshold[i] = Cvar_Get( Com_LocalClientCvarName(i, "in_joystickThreshold"), "0.15", CVAR_ARCHIVE );
	}

	SDL_StartTextInput( );

	mouseAvailable = ( in_mouse->value != 0 );
	IN_DeactivateMouse( qtrue );

	appState = SDL_GetWindowFlags( SDL_window );
	Cvar_SetValue( "com_unfocused",	!( appState & SDL_WINDOW_INPUT_FOCUS ) );
	Cvar_SetValue( "com_minimized", appState & SDL_WINDOW_MINIMIZED );

	IN_InitKeyLockStates( );

	IN_InitJoystick( );
	Com_DPrintf( "------------------------------------\n" );
}

/*
===============
IN_Shutdown
===============
*/
void IN_Shutdown( void )
{
	SDL_StopTextInput( );

	IN_DeactivateMouse( qtrue );
	mouseAvailable = qfalse;

	IN_ShutdownJoystick( );

	SDL_window = NULL;
}

/*
===============
IN_Restart
===============
*/
void IN_Restart( void )
{
	IN_ShutdownJoystick( );
	IN_Init( SDL_window );
}
