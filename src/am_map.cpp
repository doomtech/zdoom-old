// Emacs style mode select   -*- C++ -*- 
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
//
// $Log:$
//
// DESCRIPTION:  the automap code
//
//-----------------------------------------------------------------------------

#include <stdio.h>

#include "doomdef.h"
#include "templates.h"
#include "g_level.h"
#include "z_zone.h"
#include "doomdef.h"
#include "st_stuff.h"
#include "p_local.h"
#include "p_lnspec.h"
#include "w_wad.h"

#include "m_cheat.h"
#include "i_system.h"
#include "c_dispatch.h"

// Needs access to LFB.
#include "v_video.h"

#include "v_text.h"

// State.
#include "doomstat.h"
#include "r_state.h"

// Data.
#include "gstrings.h"

#include "am_map.h"

static int Background, YourColor, WallColor, TSWallColor,
		   FDWallColor, CDWallColor, ThingColor,
		   SecretWallColor, GridColor, XHairColor,
		   NotSeenColor,
		   LockedColor,
		   AlmostBackground,
		   IntraTeleportColor, InterTeleportColor;

static int DoomColors[11];
static byte DoomPaletteVals[11*3] =
{
	0x00,0x00,0x00, 0xff,0xff,0xff, 0x10,0x10,0x10,
	0xfc,0x00,0x00, 0x80,0x80,0x80, 0xbc,0x78,0x48,
	0xfc,0xfc,0x00, 0x74,0xfc,0x6c, 0x4c,0x4c,0x4c,
	0x80,0x80,0x80, 0x6c,0x6c,0x6c
};

static int WeightingScale;

CVAR (Bool,  am_rotate,				false,		CVAR_ARCHIVE);
CVAR (Bool,  am_overlay,			false,		CVAR_ARCHIVE);
CVAR (Bool,  am_showsecrets,		true,		CVAR_ARCHIVE);
CVAR (Bool,  am_showmonsters,		true,		CVAR_ARCHIVE);
CVAR (Bool,  am_showtime,			true,		CVAR_ARCHIVE);
CVAR (Bool,  am_usecustomcolors,	true,		CVAR_ARCHIVE);
CVAR (Float, am_ovtrans,			1.f,		CVAR_ARCHIVE);
CVAR (Color, am_backcolor,			0x6c5440,	CVAR_ARCHIVE);
CVAR (Color, am_yourcolor,			0xfce8d8,	CVAR_ARCHIVE);
CVAR (Color, am_wallcolor,			0x2c1808,	CVAR_ARCHIVE);
CVAR (Color, am_tswallcolor,		0x888888,	CVAR_ARCHIVE);
CVAR (Color, am_fdwallcolor,		0x887058,	CVAR_ARCHIVE);
CVAR (Color, am_cdwallcolor,		0x4c3820,	CVAR_ARCHIVE);
CVAR (Color, am_thingcolor,			0xfcfcfc,	CVAR_ARCHIVE);
CVAR (Color, am_gridcolor,			0x8b5a2b,	CVAR_ARCHIVE);
CVAR (Color, am_xhaircolor,			0x808080,	CVAR_ARCHIVE);
CVAR (Color, am_notseencolor,		0x6c6c6c,	CVAR_ARCHIVE);
CVAR (Color, am_lockedcolor,		0x007800,	CVAR_ARCHIVE);
CVAR (Color, am_ovyourcolor,		0xfce8d8,	CVAR_ARCHIVE);
CVAR (Color, am_ovwallcolor,		0x00ff00,	CVAR_ARCHIVE);
CVAR (Color, am_ovthingcolor,		0xe88800,	CVAR_ARCHIVE);
CVAR (Color, am_ovotherwallscolor,	0x008844,	CVAR_ARCHIVE);
CVAR (Color, am_ovunseencolor,		0x00226e,	CVAR_ARCHIVE);
CVAR (Color, am_ovtelecolor,		0xffff00,	CVAR_ARCHIVE);
CVAR (Color, am_intralevelcolor,	0x0000ff,	CVAR_ARCHIVE);
CVAR (Color, am_interlevelcolor,	0xff0000,	CVAR_ARCHIVE);

// drawing stuff
#define	FB		(screen)

#define AM_PANDOWNKEY	KEY_DOWNARROW
#define AM_PANUPKEY		KEY_UPARROW
#define AM_PANRIGHTKEY	KEY_RIGHTARROW
#define AM_PANLEFTKEY	KEY_LEFTARROW
#define AM_ZOOMINKEY	KEY_EQUALS
#define AM_ZOOMINKEY2	0x4e	// DIK_ADD
#define AM_ZOOMOUTKEY	KEY_MINUS
#define AM_ZOOMOUTKEY2	0x4a	// DIK_SUBTRACT
#define AM_GOBIGKEY		0x0b	// DIK_0
#define AM_FOLLOWKEY	'f'
#define AM_GRIDKEY		'g'
#define AM_MARKKEY		'm'
#define AM_CLEARMARKKEY	'c'

#define AM_NUMMARKPOINTS 10

// scale on entry
#define INITSCALEMTOF (.2*FRACUNIT)
// how much the automap moves window per tic in frame-buffer coordinates
// moves 140 pixels in 1 second
#define F_PANINC	(140/TICRATE)
// how much zoom-in per tic
// goes to 2x in 1 second
#define M_ZOOMIN        ((int) (1.02*FRACUNIT))
// how much zoom-out per tic
// pulls out to 0.5x in 1 second
#define M_ZOOMOUT       ((int) (FRACUNIT/1.02))

// translates between frame-buffer and map distances
#define FTOM(x) FixedMul(((x)<<16),scale_ftom)
#define MTOF(x) (FixedMul((x),scale_mtof)>>16)
// translates between frame-buffer and map coordinates
#define CXMTOF(x)  (MTOF((x)-m_x)/* - f_x*/)
#define CYMTOF(y)  (f_h - MTOF((y)-m_y)/* + f_y*/)

typedef struct {
	int x, y;
} fpoint_t;

typedef struct {
	fpoint_t a, b;
} fline_t;

typedef struct {
	fixed_t x,y;
} mpoint_t;

typedef struct {
	mpoint_t a, b;
} mline_t;

typedef struct {
	fixed_t slp, islp;
} islope_t;



//
// The vector graphics for the automap.
//  A line drawing of the player pointing right,
//   starting from the middle.
//
#define R ((8*PLAYERRADIUS)/7)
mline_t player_arrow[] = {
	{ { -R+R/8, 0 }, { R, 0 } }, // -----
	{ { R, 0 }, { R-R/2, R/4 } },  // ----->
	{ { R, 0 }, { R-R/2, -R/4 } },
	{ { -R+R/8, 0 }, { -R-R/8, R/4 } }, // >---->
	{ { -R+R/8, 0 }, { -R-R/8, -R/4 } },
	{ { -R+3*R/8, 0 }, { -R+R/8, R/4 } }, // >>--->
	{ { -R+3*R/8, 0 }, { -R+R/8, -R/4 } }
};
#undef R
#define NUMPLYRLINES (sizeof(player_arrow)/sizeof(mline_t))

#define R ((8*PLAYERRADIUS)/7)
mline_t cheat_player_arrow[] = {
	{ { -R+R/8, 0 }, { R, 0 } }, // -----
	{ { R, 0 }, { R-R/2, R/6 } },  // ----->
	{ { R, 0 }, { R-R/2, -R/6 } },
	{ { -R+R/8, 0 }, { -R-R/8, R/6 } }, // >----->
	{ { -R+R/8, 0 }, { -R-R/8, -R/6 } },
	{ { -R+3*R/8, 0 }, { -R+R/8, R/6 } }, // >>----->
	{ { -R+3*R/8, 0 }, { -R+R/8, -R/6 } },
	{ { -R/2, 0 }, { -R/2, -R/6 } }, // >>-d--->
	{ { -R/2, -R/6 }, { -R/2+R/6, -R/6 } },
	{ { -R/2+R/6, -R/6 }, { -R/2+R/6, R/4 } },
	{ { -R/6, 0 }, { -R/6, -R/6 } }, // >>-dd-->
	{ { -R/6, -R/6 }, { 0, -R/6 } },
	{ { 0, -R/6 }, { 0, R/4 } },
	{ { R/6, R/4 }, { R/6, -R/7 } }, // >>-ddt->
	{ { R/6, -R/7 }, { R/6+R/32, -R/7-R/32 } },
	{ { R/6+R/32, -R/7-R/32 }, { R/6+R/10, -R/7 } }
};
#undef R
#define NUMCHEATPLYRLINES (sizeof(cheat_player_arrow)/sizeof(mline_t))

#define R (FRACUNIT)
// [RH] Avoid lots of warnings without compiler-specific #pragmas
#define L(a,b,c,d) { {(fixed_t)((a)*R),(fixed_t)((b)*R)}, {(fixed_t)((c)*R),(fixed_t)((d)*R)} }
mline_t triangle_guy[] = {
	L (-.867,-.5, .867,-.5),
	L (.867,-.5, 0,1),
	L (0,1, -.867,-.5)
};
#define NUMTRIANGLEGUYLINES (sizeof(triangle_guy)/sizeof(mline_t))

mline_t thintriangle_guy[] = {
	L (-.5,-.7, 1,0),
	L (1,0, -.5,.7),
	L (-.5,.7, -.5,-.7)
};
#undef L
#undef R
#define NUMTHINTRIANGLEGUYLINES (sizeof(thintriangle_guy)/sizeof(mline_t))




int			AutoMapCheat = 0;
static int 	grid = 0;

static int 	leveljuststarted = 1; 	// kluge until AM_LevelInit() is called

bool		automapactive = false;

// location of window on screen
static int	f_x;
static int	f_y;

// size of window on screen
static int	f_w;
static int	f_h;
static int	f_p;				// [RH] # of bytes from start of a line to start of next

static byte *fb;				// pseudo-frame buffer
static int	amclock;

static mpoint_t	m_paninc;		// how far the window pans each tic (map coords)
static fixed_t	mtof_zoommul;	// how far the window zooms in each tic (map coords)
static fixed_t	ftom_zoommul;	// how far the window zooms in each tic (fb coords)

static fixed_t	m_x, m_y;		// LL x,y where the window is on the map (map coords)
static fixed_t	m_x2, m_y2;		// UR x,y where the window is on the map (map coords)

//
// width/height of window on map (map coords)
//
static fixed_t	m_w;
static fixed_t	m_h;

// based on level size
static fixed_t	min_x;
static fixed_t	min_y; 
static fixed_t	max_x;
static fixed_t	max_y;

static fixed_t	max_w; // max_x-min_x,
static fixed_t	max_h; // max_y-min_y

// based on player size
static fixed_t	min_w;
static fixed_t	min_h;


static fixed_t	min_scale_mtof; // used to tell when to stop zooming out
static fixed_t	max_scale_mtof; // used to tell when to stop zooming in

// old stuff for recovery later
static fixed_t old_m_w, old_m_h;
static fixed_t old_m_x, old_m_y;

// old location used by the Follower routine
static mpoint_t f_oldloc;

// used by MTOF to scale from map-to-frame-buffer coords
static fixed_t scale_mtof = (fixed_t)INITSCALEMTOF;
// used by FTOM to scale from frame-buffer-to-map coords (=1/scale_mtof)
static fixed_t scale_ftom;

static int marknums[10]; // numbers used for marking by the automap
static mpoint_t markpoints[AM_NUMMARKPOINTS]; // where the points are
static int markpointnum = 0; // next point to be assigned

static int followplayer = 1; // specifies whether to follow the player around

static BOOL stopped = true;


#define NUMALIASES		3
#define WALLCOLORS		-1
#define FDWALLCOLORS	-2
#define CDWALLCOLORS	-3

#define WEIGHTBITS		6
#define WEIGHTSHIFT		(FRACBITS-WEIGHTBITS)
#define NUMWEIGHTS		(1<<WEIGHTBITS)
#define WEIGHTMASK		(NUMWEIGHTS-1)
static byte antialias[NUMALIASES][NUMWEIGHTS];



void AM_rotatePoint (fixed_t *x, fixed_t *y);

void DrawWuLine (int X0, int Y0, int X1, int Y1, byte *BaseColor);
void DrawTransWuLine (int X0, int Y0, int X1, int Y1, byte BaseColor);

// Calculates the slope and slope according to the x-axis of a line
// segment in map coordinates (with the upright y-axis n' all) so
// that it can be used with the brain-dead drawing stuff.

// Ripped out for Heretic
/*
void AM_getIslope (mline_t *ml, islope_t *is)
{
	int dx, dy;

	dy = ml->a.y - ml->b.y;
	dx = ml->b.x - ml->a.x;
	if (!dy) is->islp = (dx<0?-MAXINT:MAXINT);
		else is->islp = FixedDiv(dx, dy);
	if (!dx) is->slp = (dy<0?-MAXINT:MAXINT);
		else is->slp = FixedDiv(dy, dx);
}
*/

//
//
//
void AM_activateNewScale ()
{
	m_x += m_w/2;
	m_y += m_h/2;
	m_w = FTOM(f_w);
	m_h = FTOM(f_h);
	m_x -= m_w/2;
	m_y -= m_h/2;
	m_x2 = m_x + m_w;
	m_y2 = m_y + m_h;
}

//
//
//
void AM_saveScaleAndLoc ()
{
	old_m_x = m_x;
	old_m_y = m_y;
	old_m_w = m_w;
	old_m_h = m_h;
}

//
//
//
void AM_restoreScaleAndLoc ()
{
	m_w = old_m_w;
	m_h = old_m_h;
	if (!followplayer)
	{
		m_x = old_m_x;
		m_y = old_m_y;
    }
	else
	{
		m_x = players[consoleplayer].camera->x - m_w/2;
		m_y = players[consoleplayer].camera->y - m_h/2;
    }
	m_x2 = m_x + m_w;
	m_y2 = m_y + m_h;

	// Change the scaling multipliers
	scale_mtof = FixedDiv(f_w<<FRACBITS, m_w);
	scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
}

//
// adds a marker at the current location
//
void AM_addMark ()
{
	markpoints[markpointnum].x = m_x + m_w/2;
	markpoints[markpointnum].y = m_y + m_h/2;
	markpointnum = (markpointnum + 1) % AM_NUMMARKPOINTS;
}

//
// Determines bounding box of all vertices,
// sets global variables controlling zoom range.
//
void AM_findMinMaxBoundaries ()
{
	int i;
	fixed_t a;
	fixed_t b;

	min_x = min_y = FIXED_MAX;
	max_x = max_y = FIXED_MIN;
  
	for (i=0;i<numvertexes;i++) {
		if (vertexes[i].x < min_x)
			min_x = vertexes[i].x;
		else if (vertexes[i].x > max_x)
			max_x = vertexes[i].x;
    
		if (vertexes[i].y < min_y)
			min_y = vertexes[i].y;
		else if (vertexes[i].y > max_y)
			max_y = vertexes[i].y;
	}
  
	max_w = max_x - min_x;
	max_h = max_y - min_y;

	min_w = 2*PLAYERRADIUS; // const? never changed?
	min_h = 2*PLAYERRADIUS;

	a = FixedDiv (SCREENWIDTH << FRACBITS, max_w);
	b = FixedDiv (SCREENHEIGHT << FRACBITS, max_h);

	min_scale_mtof = a < b ? a : b;
	max_scale_mtof = FixedDiv (SCREENHEIGHT << FRACBITS, 2*PLAYERRADIUS);
}


//
//
//
void AM_changeWindowLoc ()
{
	if (m_paninc.x || m_paninc.y) {
		followplayer = 0;
		f_oldloc.x = FIXED_MAX;
	}

	m_x += m_paninc.x;
	m_y += m_paninc.y;

	if (m_x + m_w/2 > max_x)
		m_x = max_x - m_w/2;
	else if (m_x + m_w/2 < min_x)
		m_x = min_x - m_w/2;
  
	if (m_y + m_h/2 > max_y)
		m_y = max_y - m_h/2;
	else if (m_y + m_h/2 < min_y)
		m_y = min_y - m_h/2;

	m_x2 = m_x + m_w;
	m_y2 = m_y + m_h;
}


//
//
//
void AM_initVariables ()
{
	int pnum;

	automapactive = true;

	f_oldloc.x = FIXED_MAX;
	amclock = 0;

	m_paninc.x = m_paninc.y = 0;
	ftom_zoommul = FRACUNIT;
	mtof_zoommul = FRACUNIT;

	m_w = FTOM(SCREENWIDTH);
	m_h = FTOM(SCREENHEIGHT);

	// find player to center on initially
	if (!playeringame[pnum = consoleplayer])
		for (pnum=0;pnum<MAXPLAYERS;pnum++)
			if (playeringame[pnum])
				break;
  
	m_x = players[pnum].camera->x - m_w/2;
	m_y = players[pnum].camera->y - m_h/2;
	AM_changeWindowLoc();

	// for saving & restoring
	old_m_x = m_x;
	old_m_y = m_y;
	old_m_w = m_w;
	old_m_h = m_h;
}

static void GetComponents (int color, DWORD *palette, float &r, float &g, float &b)
{
	if (palette)
		color = palette[color];

	r = (float)RPART(color);
	g = (float)GPART(color);
	b = (float)BPART(color);
}

static void AM_initColors (BOOL overlayed)
{
	static DWORD *lastpal = NULL;
	static int lastback = -1;
	DWORD *palette;
	
	palette = (DWORD *)GPalette.BaseColors;

	if (lastpal != palette)
	{
		int i, j;

		for (i = j = 0; i < 11; i++, j += 3)
		{
			DoomColors[i] = palette
				? ColorMatcher.Pick (DoomPaletteVals[j], DoomPaletteVals[j+1], DoomPaletteVals[j+2])
				: MAKERGB(DoomPaletteVals[j], DoomPaletteVals[j+1], DoomPaletteVals[j+2]);
		}
	}

	if (overlayed)
	{
		YourColor = am_ovyourcolor.GetIndex ();
		SecretWallColor = WallColor = am_ovwallcolor.GetIndex ();
		ThingColor = am_ovthingcolor.GetIndex ();
		FDWallColor = CDWallColor = LockedColor = am_ovotherwallscolor.GetIndex ();
		NotSeenColor = TSWallColor = am_ovunseencolor.GetIndex ();
		IntraTeleportColor = InterTeleportColor = am_ovtelecolor.GetIndex ();
	}
	else if (am_usecustomcolors)
	{
		/* Use the custom colors in the am_* cvars */
		Background = am_backcolor.GetIndex ();
		YourColor = am_yourcolor.GetIndex ();
		SecretWallColor = WallColor = am_wallcolor.GetIndex ();
		TSWallColor = am_tswallcolor.GetIndex ();
		FDWallColor = am_fdwallcolor.GetIndex ();
		CDWallColor = am_cdwallcolor.GetIndex ();
		ThingColor = am_thingcolor.GetIndex ();
		GridColor = am_gridcolor.GetIndex ();
		XHairColor = am_xhaircolor.GetIndex ();
		NotSeenColor = am_notseencolor.GetIndex ();
		LockedColor = am_lockedcolor.GetIndex ();
		InterTeleportColor = am_interlevelcolor.GetIndex ();
		IntraTeleportColor = am_intralevelcolor.GetIndex ();

		DWORD ba = am_backcolor;

		int r = RPART(ba) - 16;
		int g = GPART(ba) - 16;
		int b = BPART(ba) - 16;

		if (r < 0)
			r += 32;
		if (g < 0)
			g += 32;
		if (b < 0)
			b += 32;

		AlmostBackground = ColorMatcher.Pick (r, g, b);
	}
	else
	{ // Use colors corresponding to the original Doom's
		Background = DoomColors[0];
		YourColor = DoomColors[1];
		AlmostBackground = DoomColors[2];
		SecretWallColor =
			WallColor = DoomColors[3];
		TSWallColor = DoomColors[4];
		FDWallColor = DoomColors[5];
		LockedColor =
			CDWallColor = DoomColors[6];
		ThingColor = DoomColors[7];
		GridColor = DoomColors[8];
		XHairColor = DoomColors[9];
		NotSeenColor = DoomColors[10];
	}

	// initialize the anti-aliased lines
	static struct
	{
		int *color;
		int prevcolor;
		int falseColor;
	} aliasedLines[3] = {
		{ &WallColor, -1, WALLCOLORS },
		{ &FDWallColor, -1, FDWALLCOLORS },
		{ &CDWallColor, -1, CDWALLCOLORS }
	};
	float backRed, backGreen, backBlue;

	GetComponents (Background, palette, backRed, backGreen, backBlue);

	for (int alias = 0; alias < NUMALIASES; alias++)
	{
		if (aliasedLines[alias].prevcolor != *(aliasedLines[alias].color) ||
			lastpal != palette || lastback != Background)
		{
			float foreRed, foreGreen, foreBlue;

			aliasedLines[alias].prevcolor = *(aliasedLines[alias].color);
			GetComponents (*(aliasedLines[alias].color), palette, foreRed, foreGreen, foreBlue);

			for (int i = 0; i < NUMWEIGHTS; i++)
			{
				float step = (float)i;
				float fore = (NUMWEIGHTS-1 - step) / (NUMWEIGHTS-1);
				float back = step / (NUMWEIGHTS-1);
				int red = (int)(backRed * back + foreRed * fore);
				int green = (int)(backGreen * back + foreGreen * fore);
				int blue = (int)(backGreen * back + foreBlue * fore);
				if (palette)
					antialias[alias][i] = ColorMatcher.Pick (red, green, blue);
				else
					antialias[alias][i] = MAKERGB(red, green, blue);
			}
			*(aliasedLines[alias].color) = aliasedLines[alias].falseColor;
		}
	}
	lastpal = palette;
	lastback = Background;
}

//
// 
//
void AM_loadPics ()
{
	int i;
	char namebuf[9];
  
	for (i = 0; i < 10; i++)
	{
		sprintf (namebuf, "AMMNUM%d", i);
		marknums[i] = R_CheckTileNumForName (namebuf, TILE_Patch);
		if (marknums[i] != -1)
		{
			R_CacheTileNum (marknums[i], PU_CACHE);
		}
	}
}

void AM_unloadPics ()
{
}

void AM_clearMarks ()
{
	int i;

	for (i = AM_NUMMARKPOINTS-1; i >= 0; i--)
		markpoints[i].x = -1; // means empty
	markpointnum = 0;
}

//
// should be called at the start of every level
// right now, i figure it out myself
//
void AM_LevelInit ()
{
	leveljuststarted = 0;

	AM_clearMarks();

	AM_findMinMaxBoundaries();
	scale_mtof = FixedDiv(min_scale_mtof, (int) (0.7*FRACUNIT));
	if (scale_mtof > max_scale_mtof)
		scale_mtof = min_scale_mtof;
	scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
}




//
//
//
void AM_Stop ()
{
	AM_unloadPics ();
	automapactive = false;
	stopped = true;
	BorderNeedRefresh = screen->GetPageCount ();
	viewactive = true;
}

//
//
//
void AM_Start ()
{
	static char lastmap[8] = "";

	if (!stopped) AM_Stop();
	stopped = false;
	if (strncmp (lastmap, level.mapname, 8))
	{
		AM_LevelInit();
		strncpy (lastmap, level.mapname, 8);
	}
	AM_initVariables();
	AM_loadPics();
}

//
// set the window scale to the maximum size
//
void AM_minOutWindowScale ()
{
	scale_mtof = min_scale_mtof;
	scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
}

//
// set the window scale to the minimum size
//
void AM_maxOutWindowScale ()
{
	scale_mtof = max_scale_mtof;
	scale_ftom = FixedDiv(FRACUNIT, scale_mtof);
}


CCMD (togglemap)
{
	if (gamestate != GS_LEVEL)
		return;

	SB_state = screen->GetPageCount ();
	if (!automapactive)
	{
		AM_Start ();
		viewactive = (am_overlay != 0.f);
	}
	else
	{
		if (am_overlay && viewactive)
		{
			viewactive = false;
			SB_state = screen->GetPageCount ();
		}
		else
		{
			AM_Stop ();
		}
	}
}

//
// Handle events (user inputs) in automap mode
//
BOOL AM_Responder (event_t *ev)
{
	int rc;
	static int cheatstate = 0;
	static int bigstate = 0;

	rc = false;

	if (automapactive && ev->type == EV_KeyDown)
	{
		rc = true;
		switch (ev->data1)
		{
		case AM_PANRIGHTKEY: // pan right
			if (!followplayer)
				m_paninc.x = FTOM(F_PANINC);
			else
				rc = false;
			break;
		case AM_PANLEFTKEY: // pan left
			if (!followplayer)
				m_paninc.x = -FTOM(F_PANINC);
			else
				rc = false;
			break;
		case AM_PANUPKEY: // pan up
			if (!followplayer)
				m_paninc.y = FTOM(F_PANINC);
			else
				rc = false;
			break;
		case AM_PANDOWNKEY: // pan down
			if (!followplayer)
				m_paninc.y = -FTOM(F_PANINC);
			else
				rc = false;
			break;
		case AM_ZOOMOUTKEY: // zoom out
		case AM_ZOOMOUTKEY2:
			mtof_zoommul = M_ZOOMOUT;
			ftom_zoommul = M_ZOOMIN;
			break;
		case AM_ZOOMINKEY: // zoom in
		case AM_ZOOMINKEY2:
			mtof_zoommul = M_ZOOMIN;
			ftom_zoommul = M_ZOOMOUT;
			break;
		case AM_GOBIGKEY:
			bigstate = !bigstate;
			if (bigstate)
			{
				AM_saveScaleAndLoc();
				AM_minOutWindowScale();
			}
			else
				AM_restoreScaleAndLoc();
			break;
		default:
			switch (ev->data2)
			{
			case AM_FOLLOWKEY:
				followplayer = !followplayer;
				f_oldloc.x = FIXED_MAX;
				Printf ("%s\n", GStrings(followplayer ? AMSTR_FOLLOWON : AMSTR_FOLLOWOFF));
				break;
			case AM_GRIDKEY:
				grid = !grid;
				Printf ("%s\n", GStrings(grid ? AMSTR_GRIDON : AMSTR_GRIDOFF));
				break;
			case AM_MARKKEY:
				Printf ("%s %d\n", GStrings(AMSTR_MARKEDSPOT), markpointnum);
				AM_addMark();
				break;
			case AM_CLEARMARKKEY:
				AM_clearMarks();
				Printf ("%s\n", GStrings(AMSTR_MARKSCLEARED));
				break;
			default:
				cheatstate = 0;
				rc = false;
			}
		}
	}
	else if (ev->type == EV_KeyUp)
	{
		rc = false;
		switch (ev->data1)
		{
		case AM_PANRIGHTKEY:
			if (!followplayer) m_paninc.x = 0;
			break;
		case AM_PANLEFTKEY:
			if (!followplayer) m_paninc.x = 0;
			break;
		case AM_PANUPKEY:
			if (!followplayer) m_paninc.y = 0;
			break;
		case AM_PANDOWNKEY:
			if (!followplayer) m_paninc.y = 0;
			break;
		case AM_ZOOMOUTKEY:
		case AM_ZOOMOUTKEY2:
		case AM_ZOOMINKEY:
		case AM_ZOOMINKEY2:
			mtof_zoommul = FRACUNIT;
			ftom_zoommul = FRACUNIT;
			break;
		}
	}

	return rc;
}


//
// Zooming
//
void AM_changeWindowScale ()
{
	// Change the scaling multipliers
	scale_mtof = FixedMul(scale_mtof, mtof_zoommul);
	scale_ftom = FixedDiv(FRACUNIT, scale_mtof);

	if (scale_mtof < min_scale_mtof)
		AM_minOutWindowScale();
	else if (scale_mtof > max_scale_mtof)
		AM_maxOutWindowScale();
}


//
//
//
void AM_doFollowPlayer ()
{
    if (f_oldloc.x != players[consoleplayer].camera->x ||
		f_oldloc.y != players[consoleplayer].camera->y)
	{
		m_x = FTOM(MTOF(players[consoleplayer].camera->x)) - m_w/2;
		m_y = FTOM(MTOF(players[consoleplayer].camera->y)) - m_h/2;
		m_x2 = m_x + m_w;
		m_y2 = m_y + m_h;
		f_oldloc.x = players[consoleplayer].camera->x;
		f_oldloc.y = players[consoleplayer].camera->y;
	}
}

//
// Updates on Game Tick
//
void AM_Ticker ()
{
	if (!automapactive)
		return;

	amclock++;

	if (followplayer)
		AM_doFollowPlayer();

	// Change the zoom if necessary
	if (ftom_zoommul != FRACUNIT)
		AM_changeWindowScale();

	// Change x,y location
	if (m_paninc.x || m_paninc.y)
		AM_changeWindowLoc();
}


//
// Clear automap frame buffer.
//
void AM_clearFB (int color)
{
	screen->Clear (0, 0, f_w, f_h, color);
}


//
// Automap clipping of lines.
//
// Based on Cohen-Sutherland clipping algorithm but with a slightly
// faster reject and precalculated slopes.  If the speed is needed,
// use a hash algorithm to handle the common cases.
//
BOOL AM_clipMline (mline_t *ml, fline_t *fl)
{
	enum {
		LEFT	=1,
		RIGHT	=2,
		BOTTOM	=4,
		TOP		=8
	};

	register int outcode1 = 0;
	register int outcode2 = 0;
	register int outside;

	fpoint_t tmp;
	int dx;
	int dy;

#define DOOUTCODE(oc, mx, my) \
	(oc) = 0; \
	if ((my) < 0) (oc) |= TOP; \
	else if ((my) >= f_h) (oc) |= BOTTOM; \
	if ((mx) < 0) (oc) |= LEFT; \
	else if ((mx) >= f_w) (oc) |= RIGHT;

	// do trivial rejects and outcodes
	if (ml->a.y > m_y2)
		outcode1 = TOP;
	else if (ml->a.y < m_y)
		outcode1 = BOTTOM;

	if (ml->b.y > m_y2)
		outcode2 = TOP;
	else if (ml->b.y < m_y)
		outcode2 = BOTTOM;

	if (outcode1 & outcode2)
		return false; // trivially outside

	if (ml->a.x < m_x)
		outcode1 |= LEFT;
	else if (ml->a.x > m_x2)
		outcode1 |= RIGHT;

	if (ml->b.x < m_x)
		outcode2 |= LEFT;
	else if (ml->b.x > m_x2)
		outcode2 |= RIGHT;

	if (outcode1 & outcode2)
		return false; // trivially outside

	// transform to frame-buffer coordinates.
	fl->a.x = CXMTOF(ml->a.x);
	fl->a.y = CYMTOF(ml->a.y);
	fl->b.x = CXMTOF(ml->b.x);
	fl->b.y = CYMTOF(ml->b.y);

	DOOUTCODE(outcode1, fl->a.x, fl->a.y);
	DOOUTCODE(outcode2, fl->b.x, fl->b.y);

	if (outcode1 & outcode2)
		return false;

	while (outcode1 | outcode2) {
		// may be partially inside box
		// find an outside point
		if (outcode1)
			outside = outcode1;
		else
			outside = outcode2;
	
		// clip to each side
		if (outside & TOP)
		{
			dy = fl->a.y - fl->b.y;
			dx = fl->b.x - fl->a.x;
			tmp.x = fl->a.x + (dx*(fl->a.y))/dy;
			tmp.y = 0;
		}
		else if (outside & BOTTOM)
		{
			dy = fl->a.y - fl->b.y;
			dx = fl->b.x - fl->a.x;
			tmp.x = fl->a.x + (dx*(fl->a.y-f_h))/dy;
			tmp.y = f_h-1;
		}
		else if (outside & RIGHT)
		{
			dy = fl->b.y - fl->a.y;
			dx = fl->b.x - fl->a.x;
			tmp.y = fl->a.y + (dy*(f_w-1 - fl->a.x))/dx;
			tmp.x = f_w-1;
		}
		else if (outside & LEFT)
		{
			dy = fl->b.y - fl->a.y;
			dx = fl->b.x - fl->a.x;
			tmp.y = fl->a.y + (dy*(-fl->a.x))/dx;
			tmp.x = 0;
		}

		if (outside == outcode1)
		{
			fl->a = tmp;
			DOOUTCODE(outcode1, fl->a.x, fl->a.y);
		}
		else
		{
			fl->b = tmp;
			DOOUTCODE(outcode2, fl->b.x, fl->b.y);
		}
	
		if (outcode1 & outcode2)
			return false; // trivially outside
	}

	return true;
}
#undef DOOUTCODE


//
// Classic Bresenham w/ whatever optimizations needed for speed
//
void AM_drawFline (fline_t *fl, int color)
{
	switch (color)
	{
		case WALLCOLORS:
			DrawWuLine (fl->a.x, fl->a.y, fl->b.x, fl->b.y, &antialias[0][0]);
			break;
		case FDWALLCOLORS:
			DrawWuLine (fl->a.x, fl->a.y, fl->b.x, fl->b.y, &antialias[1][0]);
			break;
		case CDWALLCOLORS:
			DrawWuLine (fl->a.x, fl->a.y, fl->b.x, fl->b.y, &antialias[2][0]);
			break;
		default:
			DrawTransWuLine (fl->a.x, fl->a.y, fl->b.x, fl->b.y, color);
			break;
#if 0
  		{
			register int x;
			register int y;
			register int dx;
			register int dy;
			register int sx;
			register int sy;
			register int ax;
			register int ay;
			register int d;

#define PUTDOTP(xx,yy,cc) fb[(yy)*f_p+(xx)]=(cc)

			fl->a.x += f_x;
			fl->b.x += f_x;
			fl->a.y += f_y;
			fl->b.y += f_y;

			dx = fl->b.x - fl->a.x;
			ax = 2 * (dx<0 ? -dx : dx);
			sx = dx<0 ? -1 : 1;

			dy = fl->b.y - fl->a.y;
			ay = 2 * (dy<0 ? -dy : dy);
			sy = dy<0 ? -1 : 1;

			x = fl->a.x;
			y = fl->a.y;

			if (ax > ay) {
				d = ay - ax/2;
				for (;;) {
					PUTDOTP(x,y,(byte)color);
					if (x == fl->b.x)
						return;
					if (d>=0) {
						y += sy;
						d -= ax;
					}
					x += sx;
					d += ay;
				}
			} else {
				d = ax - ay/2;
				for (;;) {
					PUTDOTP(x, y, (byte)color);
					if (y == fl->b.y)
						return;
					if (d >= 0) {
						x += sx;
						d -= ay;
					}
					y += sy;
					d += ax;
				}
			}
		}
#endif
	}
}

/* Wu antialiased line drawer.
 * (X0,Y0),(X1,Y1) = line to draw
 * BaseColor = color # of first color in block used for antialiasing, the
 *          100% intensity version of the drawing color
 * NumLevels = size of color block, with BaseColor+NumLevels-1 being the
 *          0% intensity version of the drawing color
 * IntensityBits = log base 2 of NumLevels; the # of bits used to describe
 *          the intensity of the drawing color. 2**IntensityBits==NumLevels
 */
void PUTDOT (int xx, int yy,byte *cc, byte *cm)
{
	static int oldyy;
	static int oldyyshifted;
	byte *oldcc=cc;

#if 0
	if(xx < 32)
		cc += 7-(xx>>2);
	else if(xx > (finit_width - 32))
		cc += 7-((finit_width-xx) >> 2);
//	if(cc==oldcc) //make sure that we don't double fade the corners.
//	{
		if(yy < 32)
			cc += 7-(yy>>2);
		else if(yy > (finit_height - 32))
			cc += 7-((finit_height-yy) >> 2);
//	}
#endif
	if (cm != NULL && cc > cm)
	{
		cc = cm;
	}
	else if (cc > oldcc+6) // don't let the color escape from the fade table...
	{
		cc=oldcc+6;
	}
	if (yy == oldyy+1)
	{
		oldyy++;
		oldyyshifted += SCREENPITCH;
	}
	else if (yy == oldyy-1)
	{
		oldyy--;
		oldyyshifted -= SCREENPITCH;
	}
	else if (yy != oldyy)
	{
		oldyy = yy;
		oldyyshifted = yy*SCREENPITCH;
	}
	fb[oldyyshifted+xx] = *(cc);
}

void DrawWuLine (int x0, int y0, int x1, int y1, byte *baseColor)
{
	int deltaX, deltaY, xDir;

	if (viewactive)
	{
		// If the map is overlayed, use the translucent line drawer
		// code to avoid nasty discolored spots along the edges of
		// the lines. Otherwise, use this one to avoid reading from
		// the framebuffer.
		DrawTransWuLine (x0, y0, x1, y1, *baseColor);
		return;
	}

	// Make sure the line runs top to bottom
	if (y0 > y1)
	{
		int temp = y0; y0 = y1; y1 = temp;
		temp = x0; x0 = x1; x1 = temp;
	}

	// Draw the initial pixel, which is always exactly intersected by
	// the line and so needs no weighting
	PUTDOT (x0, y0, &baseColor[0], NULL);

	if ((deltaX = x1 - x0) >= 0)
	{
		xDir = 1;
	}
	else
	{
		xDir = -1;
		deltaX = -deltaX;	// make deltaX positive
	}
	// Special-case horizontal, vertical, and diagonal lines, which
	// require no weighting because they go right through the center of
    // every pixel
	if ((deltaY = y1 - y0) == 0)
	{ // horizontal line
		while (deltaX-- != 0)
		{
			x0 += xDir;
			PUTDOT (x0, y0, &baseColor[0], NULL);
		}
		return;
	}
	if (deltaX == 0)
	{ // vertical line
		do
		{
			y0++;
			PUTDOT (x0, y0, &baseColor[0], NULL);
		} while (--deltaY != 0);
		return;
	}
	if (deltaX == deltaY)
	{ // diagonal line.
		do
		{
			x0 += xDir;
			y0++;
			PUTDOT (x0, y0, &baseColor[0], NULL);
		} while (--deltaY != 0);
		return;
	}

	// Line is not horizontal, diagonal, or vertical
	fixed_t errorAcc = 0;	// initialize the line error accumulator to 0

	// Is this an X-major or Y-major line?
	if (deltaY > deltaX)
	{
		// Y-major line; calculate 16-bit fixed-point fractional part of a
		// pixel that X advances each time Y advances 1 pixel, truncating the
		// result so that we won't overrun the endpoint along the X axis
		fixed_t errorAdj = ((DWORD) deltaX << 16) / (DWORD) deltaY & 0xffff;

		// Draw all pixels other than the first and last
		if (xDir < 0)
		{
			while (--deltaY)
			{
				errorAcc += errorAdj;
				y0++;	// Y-major, so always advance Y

				// The most significant bits of ErrorAcc give us the intensity
				// weighting for this pixel, and the complement of the weighting
				// for the paired pixel
				int weighting = (errorAcc >> WEIGHTSHIFT) & WEIGHTMASK;
				PUTDOT (x0 - (errorAcc >> FRACBITS), y0, &baseColor[weighting], &baseColor[NUMWEIGHTS-1]);
				PUTDOT (x0 - (errorAcc >> FRACBITS) - 1, y0,
						&baseColor[WEIGHTMASK - weighting], &baseColor[NUMWEIGHTS-1]);
			}
		}
		else
		{
			while (--deltaY)
			{
				errorAcc += errorAdj;
				y0++;	// Y-major, so always advance Y
				int weighting = (errorAcc >> WEIGHTSHIFT) & WEIGHTMASK;
				PUTDOT (x0 + (errorAcc >> FRACBITS), y0, &baseColor[weighting], &baseColor[NUMWEIGHTS-1]);
				PUTDOT (x0 + (errorAcc >> FRACBITS) + 1, y0,
						&baseColor[WEIGHTMASK - weighting], &baseColor[NUMWEIGHTS-1]);
			}
		}
	}
	else
	{
		// It's an X-major line; calculate 16-bit fixed-point fractional part of a
		// pixel that Y advances each time X advances 1 pixel, truncating the
		// result to avoid overrunning the endpoint along the X axis
		fixed_t errorAdj = ((DWORD) deltaY << 16) / (DWORD) deltaX;

		// Draw all pixels other than the first and last
		while (--deltaX)
		{
			errorAcc += errorAdj;
			x0 += xDir;	// X-major, so always advance X
			int weighting = (errorAcc >> WEIGHTSHIFT) & WEIGHTMASK;
			PUTDOT (x0, y0 + (errorAcc >> FRACBITS), &baseColor[weighting], &baseColor[NUMWEIGHTS-1]);
			PUTDOT (x0, y0 + (errorAcc >> FRACBITS) + 1,
					&baseColor[WEIGHTMASK - weighting], &baseColor[NUMWEIGHTS-1]);
		}
	}

	// Draw the final pixel, which is always exactly intersected by the line
	// and so needs no weighting
	PUTDOT (x1, y1, &baseColor[0], NULL);
}

void PUTTRANSDOT (int xx, int yy, int basecolor, int level)
{
	static int oldyy;
	static int oldyyshifted;

#if 0
	if(xx < 32)
		cc += 7-(xx>>2);
	else if(xx > (finit_width - 32))
		cc += 7-((finit_width-xx) >> 2);
//	if(cc==oldcc) //make sure that we don't double fade the corners.
//	{
		if(yy < 32)
			cc += 7-(yy>>2);
		else if(yy > (finit_height - 32))
			cc += 7-((finit_height-yy) >> 2);
//	}
	if(cc > cm && cm != NULL)
	{
		cc = cm;
	}
	else if(cc > oldcc+6) // don't let the color escape from the fade table...
	{
		cc=oldcc+6;
	}
#endif
	if (yy == oldyy+1)
	{
		oldyy++;
		oldyyshifted += SCREENPITCH;
	}
	else if (yy == oldyy-1)
	{
		oldyy--;
		oldyyshifted -= SCREENPITCH;
	}
	else if (yy != oldyy)
	{
		oldyy = yy;
		oldyyshifted = yy*SCREENPITCH;
	}

	byte *spot = fb + oldyyshifted + xx;
	DWORD *bg2rgb = Col2RGB8[1+level];
	DWORD *fg2rgb = Col2RGB8[63-level];
	DWORD fg = fg2rgb[basecolor];
	DWORD bg = bg2rgb[*spot];
	bg = (fg+bg) | 0x1f07c1f;
	*spot = RGB32k[0][0][bg&(bg>>15)];
	TransArea++;
}

void DrawTransWuLine (int x0, int y0, int x1, int y1, byte baseColor)
{
	int deltaX, deltaY, xDir;

	if (y0 > y1)
	{
		int temp = y0; y0 = y1; y1 = temp;
		temp = x0; x0 = x1; x1 = temp;
	}

	PUTTRANSDOT (x0, y0, baseColor, 0);

	if ((deltaX = x1 - x0) >= 0)
	{
		xDir = 1;
	}
	else
	{
		xDir = -1;
		deltaX = -deltaX;
	}

	if ((deltaY = y1 - y0) == 0)
	{ // horizontal line
		if (x0 > x1)
		{
			swap (x0, x1);
		}
		memset (screen->GetBuffer() + y0*screen->GetPitch() + x0, baseColor, deltaX+1);
		return;
	}
	if (deltaX == 0)
	{ // vertical line
		byte *spot = screen->GetBuffer() + y0*screen->GetPitch() + x0;
		int pitch = screen->GetPitch ();
		do
		{
			*spot = baseColor;
			spot += pitch;
		} while (--deltaY != 0);
		return;
	}
	if (deltaX == deltaY)
	{ // diagonal line.
		byte *spot = screen->GetBuffer() + y0*screen->GetPitch() + x0;
		int advance = screen->GetPitch() + xDir;
		do
		{
			*spot = baseColor;
			spot += advance;
		} while (--deltaY != 0);
		return;
	}

	// line is not horizontal, diagonal, or vertical
	fixed_t errorAcc = 0;

	if (deltaY > deltaX)
	{ // y-major line
		fixed_t errorAdj = (((unsigned)deltaX << FRACBITS) / (unsigned)deltaY) & 0xffff;
		if (xDir < 0)
		{
			if (WeightingScale == 0)
			{
				while (--deltaY)
				{
					errorAcc += errorAdj;
					y0++;
					int weighting = (errorAcc >> WEIGHTSHIFT) & WEIGHTMASK;
					PUTTRANSDOT (x0 - (errorAcc >> FRACBITS), y0, baseColor, weighting);
					PUTTRANSDOT (x0 - (errorAcc >> FRACBITS) - 1, y0,
							baseColor, WEIGHTMASK - weighting);
				}
			}
			else
			{
				while (--deltaY)
				{
					errorAcc += errorAdj;
					y0++;
					int weighting = ((errorAcc * WeightingScale) >> (WEIGHTSHIFT+8)) & WEIGHTMASK;
					PUTTRANSDOT (x0 - (errorAcc >> FRACBITS), y0, baseColor, weighting);
					PUTTRANSDOT (x0 - (errorAcc >> FRACBITS) - 1, y0,
							baseColor, WEIGHTMASK - weighting);
				}
			}
		}
		else
		{
			if (WeightingScale == 0)
			{
				while (--deltaY)
				{
					errorAcc += errorAdj;
					y0++;
					int weighting = (errorAcc >> WEIGHTSHIFT) & WEIGHTMASK;
					PUTTRANSDOT (x0 + (errorAcc >> FRACBITS), y0, baseColor, weighting);
					PUTTRANSDOT (x0 + (errorAcc >> FRACBITS) + xDir, y0,
							baseColor, WEIGHTMASK - weighting);
				}
			}
			else
			{
				while (--deltaY)
				{
					errorAcc += errorAdj;
					y0++;
					int weighting = ((errorAcc * WeightingScale) >> (WEIGHTSHIFT+8)) & WEIGHTMASK;
					PUTTRANSDOT (x0 + (errorAcc >> FRACBITS), y0, baseColor, weighting);
					PUTTRANSDOT (x0 + (errorAcc >> FRACBITS) + xDir, y0,
							baseColor, WEIGHTMASK - weighting);
				}
			}
		}
	}
	else
	{ // x-major line
		fixed_t errorAdj = (((DWORD) deltaY << 16) / (DWORD) deltaX) & 0xffff;

		if (WeightingScale == 0)
		{
			while (--deltaX)
			{
				errorAcc += errorAdj;
				x0 += xDir;
				int weighting = (errorAcc >> WEIGHTSHIFT) & WEIGHTMASK;
				PUTTRANSDOT (x0, y0 + (errorAcc >> FRACBITS), baseColor, weighting);
				PUTTRANSDOT (x0, y0 + (errorAcc >> FRACBITS) + 1,
						baseColor, WEIGHTMASK - weighting);
			}
		}
		else
		{
			while (--deltaX)
			{
				errorAcc += errorAdj;
				x0 += xDir;
				int weighting = ((errorAcc * WeightingScale) >> (WEIGHTSHIFT+8)) & WEIGHTMASK;
				PUTTRANSDOT (x0, y0 + (errorAcc >> FRACBITS), baseColor, weighting);
				PUTTRANSDOT (x0, y0 + (errorAcc >> FRACBITS) + 1,
						baseColor, WEIGHTMASK - weighting);
			}
		}
	}

	PUTTRANSDOT (x1, y1, baseColor, 0);
}

//
// Clip lines, draw visible parts of lines.
//
void AM_drawMline (mline_t *ml, int color)
{
	static fline_t fl;

	if (AM_clipMline (ml, &fl))
		AM_drawFline (&fl, color); // draws it on frame buffer using fb coords
}



//
// Draws flat (floor/ceiling tile) aligned grid lines.
//
void AM_drawGrid (int color)
{
	fixed_t x, y;
	fixed_t start, end;
	mline_t ml;

	// Figure out start of vertical gridlines
	start = m_x;
	if ((start-bmaporgx)%(MAPBLOCKUNITS<<FRACBITS))
		start += (MAPBLOCKUNITS<<FRACBITS)
			- ((start-bmaporgx)%(MAPBLOCKUNITS<<FRACBITS));
	end = m_x + m_w;

	// draw vertical gridlines
	ml.a.y = m_y;
	ml.b.y = m_y+m_h;
	for (x = start; x < end; x += (MAPBLOCKUNITS<<FRACBITS))
	{
		ml.a.x = x;
		ml.b.x = x;
		if (am_rotate)
		{
			AM_rotatePoint (&ml.a.x, &ml.a.y);
			AM_rotatePoint (&ml.b.x, &ml.b.y);
		}
		AM_drawMline(&ml, color);
	}

	// Figure out start of horizontal gridlines
	start = m_y;
	if ((start-bmaporgy)%(MAPBLOCKUNITS<<FRACBITS))
		start += (MAPBLOCKUNITS<<FRACBITS)
			- ((start-bmaporgy)%(MAPBLOCKUNITS<<FRACBITS));
	end = m_y + m_h;

	// draw horizontal gridlines
	ml.a.x = m_x;
	ml.b.x = m_x + m_w;
	for (y=start; y<end; y+=(MAPBLOCKUNITS<<FRACBITS))
	{
		ml.a.y = y;
		ml.b.y = y;
		if (am_rotate)
		{
			AM_rotatePoint (&ml.a.x, &ml.a.y);
			AM_rotatePoint (&ml.b.x, &ml.b.y);
		}
		AM_drawMline (&ml, color);
	}
}

//
// Determines visible lines, draws them.
// This is LineDef based, not LineSeg based.
//
void AM_drawWalls ()
{
	int i;
	static mline_t l;

	for (i = 0; i < numlines; i++)
	{
		l.a.x = lines[i].v1->x;
		l.a.y = lines[i].v1->y;
		l.b.x = lines[i].v2->x;
		l.b.y = lines[i].v2->y;

		if (am_rotate)
		{
			AM_rotatePoint (&l.a.x, &l.a.y);
			AM_rotatePoint (&l.b.x, &l.b.y);
		}

		if (AutoMapCheat || (lines[i].flags & ML_MAPPED))
		{
			if ((lines[i].flags & ML_DONTDRAW) && !AutoMapCheat)
				continue;
			if (!lines[i].backsector)
			{
				AM_drawMline(&l, WallColor);
			}
			else
			{
				if ((lines[i].special == Teleport ||
					lines[i].special == Teleport_NoFog ||
					lines[i].special == Teleport_Line) &&
					GET_SPAC(lines[i].flags) != SPAC_MCROSS)
				{ // intra-level teleporters
					AM_drawMline(&l, IntraTeleportColor);
				}
				else if (lines[i].special == Teleport_NewMap ||
						 lines[i].special == Teleport_EndGame ||
						 lines[i].special == Exit_Normal ||
						 lines[i].special == Exit_Secret)
				{ // inter-level/game-ending teleporters
					AM_drawMline(&l, InterTeleportColor);
				}
				else if (lines[i].flags & ML_SECRET)
				{ // secret door
					if (AutoMapCheat)
						AM_drawMline(&l, SecretWallColor);
				    else
						AM_drawMline(&l, WallColor);
				}
				else if (lines[i].special == Door_LockedRaise ||
						 lines[i].special == ACS_LockedExecute)
				{
					AM_drawMline (&l, LockedColor);  // locked special
				}
				else if (lines[i].backsector->floorplane
					  != lines[i].frontsector->floorplane)
				{
					AM_drawMline(&l, FDWallColor); // floor level change
				}
				else if (lines[i].backsector->ceilingplane
					  != lines[i].frontsector->ceilingplane)
				{
					AM_drawMline(&l, CDWallColor); // ceiling level change
				}
				else if (AutoMapCheat)
				{
					AM_drawMline(&l, TSWallColor);
				}
			}
		}
		else if (players[consoleplayer].powers[pw_allmap])
		{
			if (!(lines[i].flags & ML_DONTDRAW))
				AM_drawMline(&l, NotSeenColor);
		}
    }
}


//
// Rotation in 2D.
// Used to rotate player arrow line character.
//
void AM_rotate (fixed_t *x, fixed_t *y, angle_t a)
{
	fixed_t tmpx;

	tmpx =
		FixedMul(*x,finecosine[a>>ANGLETOFINESHIFT])
		- FixedMul(*y,finesine[a>>ANGLETOFINESHIFT]);

	*y =
		FixedMul(*x,finesine[a>>ANGLETOFINESHIFT])
		+ FixedMul(*y,finecosine[a>>ANGLETOFINESHIFT]);

	*x = tmpx;
}

void AM_rotatePoint (fixed_t *x, fixed_t *y)
{
	*x -= players[consoleplayer].camera->x;
	*y -= players[consoleplayer].camera->y;
	AM_rotate (x, y, ANG90 - players[consoleplayer].camera->angle);
	*x += players[consoleplayer].camera->x;
	*y += players[consoleplayer].camera->y;
}

void
AM_drawLineCharacter
( mline_t*	lineguy,
  int		lineguylines,
  fixed_t	scale,
  angle_t	angle,
  int		color,
  fixed_t	x,
  fixed_t	y )
{
	int		i;
	mline_t	l;

	for (i=0;i<lineguylines;i++) {
		l.a.x = lineguy[i].a.x;
		l.a.y = lineguy[i].a.y;

		if (scale) {
			l.a.x = FixedMul(scale, l.a.x);
			l.a.y = FixedMul(scale, l.a.y);
		}

		if (angle)
			AM_rotate(&l.a.x, &l.a.y, angle);

		l.a.x += x;
		l.a.y += y;

		l.b.x = lineguy[i].b.x;
		l.b.y = lineguy[i].b.y;

		if (scale) {
			l.b.x = FixedMul(scale, l.b.x);
			l.b.y = FixedMul(scale, l.b.y);
		}

		if (angle)
			AM_rotate(&l.b.x, &l.b.y, angle);

		l.b.x += x;
		l.b.y += y;

		AM_drawMline(&l, color);
	}
}

void AM_drawPlayers ()
{
	angle_t angle;
	int i;

	if (!multiplayer)
	{
		if (am_rotate)
			angle = ANG90;
		else
			angle = players[consoleplayer].camera->angle;

		if (AutoMapCheat)
			AM_drawLineCharacter
			(cheat_player_arrow, NUMCHEATPLYRLINES, 0,
			 angle, YourColor, players[consoleplayer].camera->x, players[consoleplayer].camera->y);
		else
			AM_drawLineCharacter
			(player_arrow, NUMPLYRLINES, 0, angle,
			 YourColor, players[consoleplayer].camera->x, players[consoleplayer].camera->y);
		return;
	}

	for (i = 0; i < MAXPLAYERS; i++)
	{
		player_t *p = &players[i];
		int color;
		mpoint_t pt;

		if (!playeringame[i] ||
			(deathmatch && !demoplayback) && p != players[consoleplayer].camera->player)
		{
			continue;
		}

		if (p->powers[pw_invisibility])
			color = AlmostBackground;
		else
			color = ColorMatcher.Pick
				(RPART(p->userinfo.color), GPART(p->userinfo.color), BPART(p->userinfo.color));
				
		pt.x = p->mo->x;
		pt.y = p->mo->y;
		angle = p->mo->angle;

		if (am_rotate)
		{
			AM_rotatePoint (&pt.x, &pt.y);
			angle -= players[consoleplayer].camera->angle - ANG90;
		}

		AM_drawLineCharacter
			(player_arrow, NUMPLYRLINES, 0, angle,
			 color, pt.x, pt.y);
    }
}

void AM_drawThings (int color)
{
	int		 i;
	AActor*	 t;
	mpoint_t p;
	angle_t	 angle;

	for (i=0;i<numsectors;i++)
	{
		t = sectors[i].thinglist;
		while (t)
		{
			p.x = t->x;
			p.y = t->y;
			angle = t->angle;

			if (am_rotate)
			{
				AM_rotatePoint (&p.x, &p.y);
				angle += ANG90 - players[consoleplayer].camera->angle;
			}

			AM_drawLineCharacter
			(thintriangle_guy, NUMTHINTRIANGLEGUYLINES,
			 16<<FRACBITS, angle, color, p.x, p.y);
			t = t->snext;
		}
	}
}

void AM_drawMarks ()
{
	int i, fx, fy, w, h;
	mpoint_t pt;

	for (i = 0; i < AM_NUMMARKPOINTS; i++)
	{
		if (markpoints[i].x != -1)
		{
			//      w = TileSizes[i].width;
			//      h = TileSizes[i].height;
			w = 5; // because something's wrong with the wad, i guess
			h = 6; // because something's wrong with the wad, i guess

			pt.x = markpoints[i].x;
			pt.y = markpoints[i].y;

			if (am_rotate)
				AM_rotatePoint (&pt.x, &pt.y);

			fx = CXMTOF(pt.x);
			fy = CYMTOF(pt.y) - 3;

			if (fx >= f_x && fx <= f_w - w && fy >= f_y && fy <= f_h - h && marknums[i] != -1)
				FB->DrawPatchCleanNoMove (TileCache[marknums[i]], fx, fy);
		}
	}
}

void AM_drawCrosshair (int color)
{
	fb[f_p*((f_h+1)/2)+(f_w/2)] = (byte)color; // single point for now
}

void AM_Drawer ()
{
	if (!automapactive)
		return;

	AM_initColors (viewactive);

	fb = screen->GetBuffer ();
	if (!viewactive)
	{
		// [RH] Set f_? here now to handle automap overlaying
		// and view size adjustments.
		f_x = f_y = 0;
		f_w = screen->GetWidth ();
		f_h = ST_Y;
		f_p = screen->GetPitch ();
		WeightingScale = 0;

		AM_clearFB(Background);
	}
	else 
	{
		f_x = viewwindowx;
		f_y = viewwindowy;
		f_w = realviewwidth;
		f_h = realviewheight;
		f_p = screen->GetPitch ();
		WeightingScale = (int)(am_ovtrans * 256.f);
		if (WeightingScale < 0 || WeightingScale >= 256)
		{
			WeightingScale = 0;
		}
	}
	AM_activateNewScale();

	if (grid)	
		AM_drawGrid(GridColor);

	AM_drawWalls();
	AM_drawPlayers();
	if (AutoMapCheat >= 2)
		AM_drawThings(ThingColor);

	if (!viewactive)
		AM_drawCrosshair(XHairColor);

	AM_drawMarks();
}