
//**************************************************************************
//**
//** ct_chat.c : Heretic 2 : Raven Software, Corp.
//**
//** $RCSfile: ct_chat.c,v $
//** $Revision: 1.12 $
//** $Date: 96/01/16 10:35:26 $
//** $Author: bgokey $
//**
//**************************************************************************

#include <string.h>
#include <ctype.h>
#include "doomdef.h"
#include "m_swap.h"
#include "hu_stuff.h"
#include "w_wad.h"
#include "s_sound.h"
#include "doomstat.h"
#include "st_stuff.h"
#include "c_console.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "v_text.h"
#include "v_video.h"
#include "gi.h"
#include "d_gui.h"
#include "i_input.h"
#include "templates.h"

#define QUEUESIZE		128
#define MESSAGESIZE		128
#define MESSAGELEN		265
#define HU_INPUTX		0
#define HU_INPUTY		(0 + (screen->Font->GetHeight () + 1))

EXTERN_CVAR (Bool, con_scaletext)

// Public data

void CT_Init ();
void CT_Drawer ();
BOOL CT_Responder (event_t *ev);

int chatmodeon;

// Private data

static void CT_ClearChatMessage ();
static void CT_AddChar (char c);
static void CT_BackSpace ();
static void ShoveChatStr (const char *str, BYTE who);
static bool DoSubstitution (char *out, const char *in);

static int len;
static byte ChatQueue[QUEUESIZE];

CVAR (String, chatmacro1, "I'm ready to kick butt!", CVAR_ARCHIVE)
CVAR (String, chatmacro2, "I'm OK.", CVAR_ARCHIVE)
CVAR (String, chatmacro3, "I'm not looking too good!", CVAR_ARCHIVE)
CVAR (String, chatmacro4, "Help!", CVAR_ARCHIVE)
CVAR (String, chatmacro5, "You suck!", CVAR_ARCHIVE)
CVAR (String, chatmacro6, "Next time, scumbag...", CVAR_ARCHIVE)
CVAR (String, chatmacro7, "Come here!", CVAR_ARCHIVE)
CVAR (String, chatmacro8, "I'll take care of it.", CVAR_ARCHIVE)
CVAR (String, chatmacro9, "Yes", CVAR_ARCHIVE)
CVAR (String, chatmacro0, "No", CVAR_ARCHIVE)

FStringCVar *chat_macros[10] =
{
	&chatmacro0,
	&chatmacro1,
	&chatmacro2,
	&chatmacro3,
	&chatmacro4,
	&chatmacro5,
	&chatmacro6,
	&chatmacro7,
	&chatmacro8,
	&chatmacro9
};

CVAR (Bool, chat_substitution, false, CVAR_ARCHIVE)

//===========================================================================
//
// CT_Init
//
// 	Initialize chat mode data
//===========================================================================

void CT_Init ()
{
	len = 0; // initialize the queue index
	chatmodeon = 0;
	ChatQueue[0] = 0;
}

//===========================================================================
//
// CT_Stop
//
//===========================================================================

void CT_Stop ()
{
	chatmodeon = 0;
}

//===========================================================================
//
// CT_Responder
//
//===========================================================================

BOOL CT_Responder (event_t *ev)
{
	if (chatmodeon && ev->type == EV_GUI_Event)
	{
		if (ev->subtype == EV_GUI_KeyDown || ev->subtype == EV_GUI_KeyRepeat)
		{
			if (ev->data1 == '\r')
			{
				ShoveChatStr ((char *)ChatQueue, chatmodeon - 1);
				CT_Stop ();
				return true;
			}
			else if (ev->data1 == GK_ESCAPE)
			{
				CT_Stop ();
				return true;
			}
			else if (ev->data1 == '\b')
			{
				CT_BackSpace ();
				return true;
			}
			else if (ev->data1 == 'C' && (ev->data3 & GKM_CTRL))
			{
				I_PutInClipboard ((char *)ChatQueue);
				return true;
			}
			else if (ev->data1 == 'V' && (ev->data3 & GKM_CTRL))
			{
				char *clip = I_GetFromClipboard ();
				if (clip != NULL)
				{
					char *clip_p = clip;
					strtok (clip, "\n\r\b");
					while (*clip_p)
					{
						CT_AddChar (*clip_p++);
					}
					delete[] clip;
				}
			}
		}
		else if (ev->subtype == EV_GUI_Char)
		{
			// send a macro
			if (ev->data2 && (ev->data1 >= '0' && ev->data1 <= '9'))
			{
				ShoveChatStr (*chat_macros[ev->data1 - '0'], chatmodeon - 1);
				CT_Stop ();
			}
			else
			{
				CT_AddChar (ev->data1);
			}
			return true;
		}
	}

	return false;
}

//===========================================================================
//
// CT_Drawer
//
//===========================================================================

void CT_Drawer (void)
{
	if (chatmodeon)
	{
		static const char *prompt = "Say: ";
		int i, x, scalex, y, promptwidth;

		y = (viewactive || gamestate != GS_LEVEL) ? -10 : -30;
		if (con_scaletext)
		{
			scalex = CleanXfac;
			y *= CleanYfac;
		}
		else
		{
			scalex = 1;
		}

		y += ((SCREENHEIGHT == realviewheight && viewactive) || gamestate != GS_LEVEL) ? SCREENHEIGHT : ST_Y;

		promptwidth = SmallFont->StringWidth (prompt) * scalex;
		x = screen->Font->GetCharWidth ('_') * scalex * 2 + promptwidth;

		// figure out if the text is wider than the screen->
		// if so, only draw the right-most portion of it.
		for (i = len - 1; i >= 0 && x < SCREENWIDTH; i--)
		{
			x += screen->Font->GetCharWidth (ChatQueue[i] & 0x7f) * scalex;
		}

		if (i >= 0)
		{
			i++;
		}
		else
		{
			i = 0;
		}

		// draw the prompt, text, and cursor
		ChatQueue[len] = gameinfo.gametype == GAME_Doom ? '_' : '[';
		ChatQueue[len+1] = '\0';
		screen->DrawText (CR_GREEN, 0, y, prompt, DTA_CleanNoMove, *con_scaletext, TAG_DONE);
		screen->DrawText (CR_GREY, promptwidth, y, (char *)(ChatQueue + i), DTA_CleanNoMove, *con_scaletext, TAG_DONE);
		ChatQueue[len] = '\0';

		BorderTopRefresh = screen->GetPageCount ();
	}

	if (players[consoleplayer].camera != NULL && deathmatch &&
		(Button_ShowScores.bDown ||
		 players[consoleplayer].camera->health <= 0))
	{
		HU_DrawScores (&players[consoleplayer]);
	}
}

//===========================================================================
//
// CT_AddChar
//
//===========================================================================

static void CT_AddChar (char c)
{
	if (len < QUEUESIZE-2)
	{
		ChatQueue[len++] = c;
		ChatQueue[len] = 0;
	}
}

//===========================================================================
//
// CT_BackSpace
//
// 	Backs up a space, when the user hits (obviously) backspace
//===========================================================================

static void CT_BackSpace ()
{
	if (len)
	{
		ChatQueue[--len] = 0;
	}
}

//===========================================================================
//
// CT_ClearChatMessage
//
// 	Clears out the data for the chat message.
//===========================================================================

static void CT_ClearChatMessage ()
{
	ChatQueue[0] = 0;
	len = 0;
}

//===========================================================================
//
// ShoveChatStr
//
// Sends the chat message across the network
//
//===========================================================================

static void ShoveChatStr (const char *str, BYTE who)
{
	char substBuff[256];

	if (str[0] == '/' &&
		(str[1] == 'm' || str[1] == 'M') &&
		(str[2] == 'e' || str[2] == 'E'))
	{ // This is a /me message
		str += 3;
		who |= 2;
	}

	Net_WriteByte (DEM_SAY);
	Net_WriteByte (who);

	if (!chat_substitution || !DoSubstitution (substBuff, str))
	{
		Net_WriteString (str);
	}
	else
	{
		Net_WriteString (substBuff);
	}
}

//===========================================================================
//
// DoSubstitution
//
// Replace certain special substrings with different values to reflect
// the player's current state.
//
//===========================================================================

static bool DoSubstitution (char *out, const char *in)
{
	player_t *player = &players[consoleplayer];
	AWeapon *weapon = player->ReadyWeapon;
	const char *a, *b;

	a = in;
	while ((b = strchr (a, '$')))
	{
		strncpy (out, a, b - a);
		out += b - a;

		a = ++b;
		while (*b && isalpha (*b))
		{
			++b;
		}

		ptrdiff_t len = b - a;

		if (len == 6)
		{
			if (strnicmp (a, "health", 6) == 0)
			{
				out += sprintf (out, "%d", player->health);
			}
			else if (strnicmp (a, "weapon", 6) == 0)
			{
				if (weapon == NULL)
				{
					out += sprintf (out, "no weapon");
				}
				else
				{
					out += sprintf (out, "%s", weapon->GetClass()->Name+1);
				}
			}
		}
		else if (len == 5)
		{
			if (strnicmp (a, "armor", 5) == 0)
			{
				AInventory *armor = player->mo->FindInventory<ABasicArmor>();
				int armorpoints = armor != NULL ? armor->Amount : 0;
				out += sprintf (out, "%d", armorpoints);
			}
		}
		else if (len == 9)
		{
			if (strnicmp (a, "ammocount", 9) == 0)
			{
				if (weapon == NULL)
				{
					out += sprintf (out, "0");
				}
				else
				{
					out += sprintf (out, "%d", weapon->Ammo1 != NULL ? weapon->Ammo1->Amount : 0);
					if (weapon->Ammo2 != NULL)
					{
						out += sprintf (out, "/%d", weapon->Ammo2->Amount);
					}
				}
			}
		}
		else if (len == 4)
		{
			if (strnicmp (a, "ammo", 4) == 0)
			{
				if (weapon == NULL || weapon->Ammo1 == NULL)
				{
					out += sprintf (out, "no ammo");
				}
				else
				{
					out += sprintf (out, "%s", weapon->Ammo1->GetClass()->Name+1);
					if (weapon->Ammo2 != NULL)
					{
						out += sprintf (out, "/%s", weapon->Ammo2->GetClass()->Name+1);
					}
				}
			}
		}
		else if (len == 0)
		{
			*out++ = '$';
			*out = 0;
			if (*b == '$')
			{
				b++;
			}
		}
		else
		{
			*out++ = '$';
			strncpy (out, a, len);
			out += len;
		}
		a = b;
	}

	// Return false if no substitution was performed
	if (a == in)
	{
		return false;
	}

	strcpy (out, a);
	return true;
}

CCMD (messagemode)
{
	if (menuactive == MENU_Off)
	{
		chatmodeon = 1;
		C_HideConsole ();
		CT_ClearChatMessage ();
	}
}

CCMD (say)
{
	if (argv.argc() == 1)
	{
		Printf ("Usage: say <message>\n");
	}
	else
	{
		ShoveChatStr (argv[1], 0);
	}
}

CCMD (messagemode2)
{
	if (menuactive == MENU_Off)
	{
		chatmodeon = 2;
		C_HideConsole ();
		CT_ClearChatMessage ();
	}
}

CCMD (say_team)
{
	if (argv.argc() == 1)
	{
		Printf ("Usage: say_team <message>\n");
	}
	else
	{
		ShoveChatStr (argv[1], 1);
	}
}
