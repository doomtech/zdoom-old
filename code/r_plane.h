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
// DESCRIPTION:
//		Refresh, visplane stuff (floor, ceilings).
//
//-----------------------------------------------------------------------------


#ifndef __R_PLANE_H__
#define __R_PLANE_H__

#include "r_data.h"

class ASkyViewpoint;

//
// The infamous visplane
// 
struct visplane_s
{
	struct visplane_s *next;		// Next visplane in hash chain -- killough

	secplane_t	height;
	int			picnum;
	int			lightlevel;
	fixed_t		xoffs, yoffs;		// killough 2/28/98: Support scrolling flats
	int			minx, maxx;
	byte		*colormap;			// [RH] Support multiple colormaps
	fixed_t		xscale, yscale;		// [RH] Support flat scaling
	angle_t		angle;				// [RH] Support flat rotation
	ASkyViewpoint *skybox;			// [RH] Support sky boxes

	unsigned short *bottom;			// [RH] bottom and top arrays are dynamically
	unsigned short pad;				//		allocated immediately after the
	unsigned short top[3];			//		visplane.
};
typedef struct visplane_s visplane_t;



// killough 10/98: special mask indicates sky flat comes from sidedef
#define PL_SKYFLAT (0x8000)

// Visplane related.
extern ptrdiff_t		lastopening;	// type short


typedef void (*planefunction_t) (int top, int bottom);

extern planefunction_t	floorfunc;
extern planefunction_t	ceilingfunc_t;

extern short			floorclip[MAXWIDTH];
extern short			ceilingclip[MAXWIDTH];

extern fixed_t			yslope[MAXHEIGHT];

void R_InitPlanes ();
void R_ClearPlanes (bool fullclear);

void R_MakeSpans (int x, int t1, int b1, int t2, int b2, void (*mapfunc)(int y, int x1));
void R_DrawPlanes ();
void R_DrawSkyBoxes ();
void R_DrawSkyPlane (visplane_t *pl);
void R_DrawNormalPlane (visplane_t *pl);
void R_DrawTiltedPlane (visplane_t *pl);
void R_MapVisPlane (visplane_t *pl, void (*mapfunc)(int y, int x1));

visplane_t *R_FindPlane
( const secplane_t &height,
  int			picnum,
  int			lightlevel,
  fixed_t		xoffs,		// killough 2/28/98: add x-y offsets
  fixed_t		yoffs,
  fixed_t		xscale,
  fixed_t		yscale,
  angle_t		angle,
  ASkyViewpoint *skybox);

visplane_t *R_CheckPlane (visplane_t *pl, int start, int stop);

bool R_AlignFlat (int linenum, int side, int fc);

// [RH] Added for multires support
BOOL R_PlaneInitData (void);


extern visplane_t*		floorplane;
extern visplane_t*		ceilingplane;


#endif // __R_PLANE_H__