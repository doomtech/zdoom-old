// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:  Head up display
//
//-----------------------------------------------------------------------------

#ifndef __HU_STUFF_H__
#define __HU_STUFF_H__

#include "d_event.h"


//
// Globally visible constants.
//
const byte HU_FONTSTART = '!';	// the first font characters
const byte HU_FONTEND   = '�';	// the last font characters

// Calculate # of glyphs in font.
const int HU_FONTSIZE = HU_FONTEND - HU_FONTSTART + 1;

//
// Chat routines
//

void CT_Init (void);
BOOL CT_Responder (event_t* ev);
void CT_Drawer (void);

extern int chatmodeon;

// [RH] Draw deathmatch scores

class player_s;
void HU_DrawScores (player_s *me);

#endif