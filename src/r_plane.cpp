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
// $Log:$
//
// DESCRIPTION:
//		Here is a core component: drawing the floors and ceilings,
//		 while maintaining a per column clipping list only.
//		Moreover, the sky areas have to be determined.
//
// MAXVISPLANES is no longer a limit on the number of visplanes,
// but a limit on the number of hash slots; larger numbers mean
// better performance usually but after a point they are wasted,
// and memory and time overheads creep in.
//
// Lee Killough
//
// [RH] Further modified to significantly increase accuracy and add slopes.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <float.h>

#include "templates.h"
#include "i_system.h"
#include "w_wad.h"

#include "doomdef.h"
#include "doomstat.h"

#include "r_local.h"
#include "r_sky.h"
#include "stats.h"

#include "m_alloc.h"
#include "v_video.h"
#include "vectors.h"
#include "a_sharedglobal.h"

EXTERN_CVAR (Int, tx)
EXTERN_CVAR (Int, ty)

static void R_DrawSkyStriped (visplane_t *pl);

EXTERN_CVAR (Bool, r_particles);

planefunction_t 		floorfunc;
planefunction_t 		ceilingfunc;

// Here comes the obnoxious "visplane".
#define MAXVISPLANES 128    /* must be a power of 2 */

// [RH] Allocate one extra for sky box planes.
static visplane_t		*visplanes[MAXVISPLANES+1];	// killough
static visplane_t		*freetail;					// killough
static visplane_t		**freehead = &freetail;		// killough

visplane_t 				*floorplane;
visplane_t 				*ceilingplane;

// killough -- hash function for visplanes
// Empirically verified to be fairly uniform:

#define visplane_hash(picnum,lightlevel,height) \
  ((unsigned)((picnum)*3+(lightlevel)+((height).d)*7) & (MAXVISPLANES-1))

//
// opening
//

size_t					maxopenings;
short					*openings;
ptrdiff_t				lastopening;


//
// Clip values are the solid pixel bounding the range.
//	floorclip starts out SCREENHEIGHT and is just outside the range
//	ceilingclip starts out 0 and is just inside the range
//
short					floorclip[MAXWIDTH];
short					ceilingclip[MAXWIDTH];

//
// texture mapping
//

static fixed_t			planeheight;

extern "C" {
//
// spanend holds the end of a plane span in each screen row
//
short					spanend[MAXHEIGHT];
byte					*tiltlighting[MAXWIDTH];

int						planeshade;
vec3_t					plane_sz, plane_su, plane_sv;
float					planelightfloat;
bool					plane_shade;
fixed_t					pviewx, pviewy;

void R_DrawTiltedPlane_ASM (int y, int x1);
}

fixed_t 				yslope[MAXHEIGHT];
static fixed_t			xscale, yscale;
static DWORD			xstepscale, ystepscale;
static DWORD			basexfrac, baseyfrac;

#ifdef USEASM
extern "C" void R_SetSpanSource_ASM (const byte *flat);
extern "C" void STACK_ARGS R_SetSpanSize_ASM (int xbits, int ybits);
extern "C" void R_SetSpanColormap_ASM (byte *colormap);
extern "C" void R_SetTiltedSpanSource_ASM (const byte *flat);
extern "C" byte *ds_curcolormap, *ds_cursource, *ds_curtiltedsource;
#endif
void					R_DrawSinglePlane (visplane_t *, fixed_t alpha, bool masked);

//==========================================================================
//
// R_InitPlanes
//
// Called at game startup.
//
//==========================================================================

void R_InitPlanes ()
{
}

//==========================================================================
//
// R_MapPlane
//
// Globals used: planeheight, ds_source, basexscale, baseyscale,
// pviewx, pviewy, xoffs, yoffs, basecolormap, xscale, yscale.
//
//==========================================================================

void R_MapPlane (int y, int x1)
{
	int x2 = spanend[y];
	fixed_t distance;

#ifdef RANGECHECK
	if (x2 < x1 || x1<0 || x2>=viewwidth || (unsigned)y>=(unsigned)viewheight)
	{
		I_FatalError ("R_MapPlane: %i, %i at %i", x1, x2, y);
	}
#endif

	// [RH] Notice that I dumped the caching scheme used by Doom.
	// It did not offer any appreciable speedup.

	distance = FixedMul (planeheight, yslope[y]);

	ds_xstep = FixedMul (distance, xstepscale);
	ds_ystep = FixedMul (distance, ystepscale);
	ds_xfrac = FixedMul (distance, basexfrac) + pviewx;
	ds_yfrac = FixedMul (distance, baseyfrac) + pviewy;

	if (plane_shade)
	{
		// Determine lighting based on the span's distance from the viewer.
		ds_colormap = basecolormap + (GETPALOOKUP (
			FixedMul (GlobVis, abs (centeryfrac - (y << FRACBITS))), planeshade) << COLORMAPSHIFT);
	}

#ifdef USEASM
	if (ds_colormap != ds_curcolormap)
		R_SetSpanColormap_ASM (ds_colormap);
#endif

	ds_y = y;
	ds_x1 = x1;
	ds_x2 = x2;

	spanfunc ();
}

//==========================================================================
//
// R_CalcTiltedLighting
//
// Calculates the lighting for one row of a tilted plane. If the definition
// of GETPALOOKUP changes, this needs to change, too.
//
//==========================================================================

extern "C" {
void STACK_ARGS R_CalcTiltedLighting (fixed_t lval, fixed_t lend, int width)
{
	fixed_t lstep;
	byte *lightfiller;
	int i = 0;

	lval = planeshade - lval;
	lend = planeshade - lend;

	if (width == 0 || lval == lend)
	{ // Constant lighting
		lightfiller = basecolormap + (GETPALOOKUP (-lval, 0) << COLORMAPSHIFT);
	}
	else if ((lstep = (lend - lval) / width) < 0)
	{ // Going from dark to light
		if (lval < FRACUNIT)
		{ // All bright
			lightfiller = basecolormap;
		}
		else
		{
			if (lval >= NUMCOLORMAPS*FRACUNIT)
			{ // Starts beyond the dark end
				byte *clight = basecolormap + ((NUMCOLORMAPS-1) << COLORMAPSHIFT);
				while (lval >= NUMCOLORMAPS*FRACUNIT && i <= width)
				{
					tiltlighting[i++] = clight;
					lval += lstep;
				}
				if (i > width)
					return;
			}
			while (i <= width && lval >= 0)
			{
				tiltlighting[i++] = basecolormap + ((lval >> FRACBITS) << COLORMAPSHIFT);
				lval += lstep;
			}
			lightfiller = basecolormap;
		}
	}
	else
	{ // Going from light to dark
		if (lval >= (NUMCOLORMAPS-1)*FRACUNIT)
		{ // All dark
			lightfiller = basecolormap + ((NUMCOLORMAPS-1) << COLORMAPSHIFT);
		}
		else
		{
			while (lval < 0 && i <= width)
			{
				tiltlighting[i++] = basecolormap;
				lval += lstep;
			}
			if (i > width)
				return;
			while (i <= width && lval < (NUMCOLORMAPS-1)*FRACUNIT)
			{
				tiltlighting[i++] = basecolormap + ((lval >> FRACBITS) << COLORMAPSHIFT);
				lval += lstep;
			}
			lightfiller = basecolormap + ((NUMCOLORMAPS-1) << COLORMAPSHIFT);
		}
	}

	for (; i <= width; i++)
	{
		tiltlighting[i] = lightfiller;
	}
}
}	// extern "C"

//==========================================================================
//
// R_MapTiltedPlane
//
//==========================================================================

void R_MapTiltedPlane (int y, int x1)
{
	int x2 = spanend[y];
	int width = x2 - x1;
	float iz, uz, vz;
	byte *fb;
	DWORD u, v;
	int i;

	iz = plane_sz[2] + plane_sz[1]*(centery-y) + plane_sz[0]*(x1-centerx);

	// Lighting is simple. It's just linear interpolation from start to end
	if (plane_shade)
	{
		uz = (iz + plane_sz[0]*width) * planelightfloat;
		vz = iz * planelightfloat;
		R_CalcTiltedLighting (toint (vz), toint (uz), width);
	}

	uz = plane_su[2] + plane_su[1]*(centery-y) + plane_su[0]*(x1-centerx);
	vz = plane_sv[2] + plane_sv[1]*(centery-y) + plane_sv[0]*(x1-centerx);

	fb = ylookup[y] + x1 + dc_destorg;

	BYTE vshift = 32 - ds_ybits;
	BYTE ushift = vshift - ds_xbits;
	int umask = ((1 << ds_xbits) - 1) << ds_ybits;

#if 0		// The "perfect" reference version of this routine. Pretty slow.
			// Use it only to see how things are supposed to look.
	i = 0;
	do
	{
		float z = 1.f/iz;

		u = toint (uz*z) + pviewx;
		v = toint (vz*z) + pviewy;
		ds_colormap = tiltlighting[i];
		fb[i++] = ds_colormap[ds_source[(v >> vshift) | ((u >> ushift) & umask)]];
		iz += plane_sz[0];
		uz += plane_su[0];
		vz += plane_sv[0];
	} while (--width >= 0);
#else
//#define SPANSIZE 32
//#define INVSPAN 0.03125f
//#define SPANSIZE 8
//#define INVSPAN 0.125f
#define SPANSIZE 16
#define INVSPAN	0.0625f

	float startz = 1.f/iz;
	float startu = uz*startz;
	float startv = vz*startz;
	float izstep, uzstep, vzstep;

	izstep = plane_sz[0] * SPANSIZE;
	uzstep = plane_su[0] * SPANSIZE;
	vzstep = plane_sv[0] * SPANSIZE;
	x1 = 0;
	width++;

	while (width >= SPANSIZE)
	{
		iz += izstep;
		uz += uzstep;
		vz += vzstep;

		float endz = 1.f/iz;
		float endu = uz*endz;
		float endv = vz*endz;
		DWORD stepu = toint ((endu - startu) * INVSPAN);
		DWORD stepv = toint ((endv - startv) * INVSPAN);
		u = toint (startu) + pviewx;
		v = toint (startv) + pviewy;

		for (i = SPANSIZE-1; i >= 0; i--)
		{
			fb[x1] = *(tiltlighting[x1] + ds_source[(v >> vshift) | ((u >> ushift) & umask)]);
			x1++;
			u += stepu;
			v += stepv;
		}
		startu = endu;
		startv = endv;
		startz = endz;
		width -= SPANSIZE;
	}
	if (width > 0)
	{
		if (width == 1)
		{
			u = toint (startu);
			v = toint (startv);
			fb[x1] = *(tiltlighting[x1] + ds_source[(v >> vshift) | ((u >> ushift) & umask)]);
		}
		else
		{
			float left = (float)width;
			iz += plane_sz[0] * left;
			uz += plane_su[0] * left;
			vz += plane_sv[0] * left;

			float endz = 1.f/iz;
			float endu = uz*endz;
			float endv = vz*endz;
			left = 1.f/left;
			DWORD stepu = toint ((endu - startu) * left);
			DWORD stepv = toint ((endv - startv) * left);
			u = toint (startu) + pviewx;
			v = toint (startv) + pviewy;

			for (; width != 0; width--)
			{
				fb[x1] = *(tiltlighting[x1] + ds_source[(v >> vshift) | ((u >> ushift) & umask)]);
				x1++;
				u += stepu;
				v += stepv;
			}
		}
	}
#endif
}

//==========================================================================
//
// R_MapColoredPlane
//
//==========================================================================

void R_MapColoredPlane (int y, int x1)
{
	memset (ylookup[y] + x1 + dc_destorg, ds_color, spanend[y] - x1 + 1);
}

//==========================================================================
//
// R_ClearPlanes
//
// Called at the beginning of each frame.
//
//==========================================================================

extern int ConBottom;

void R_ClearPlanes (bool fullclear)
{
	int i;
	
	for (i = 0; i < MAXVISPLANES; i++)	// new code -- killough
		for (*freehead = visplanes[i], visplanes[i] = NULL; *freehead; )
			freehead = &(*freehead)->next;

	if (fullclear)
	{
		// opening / clipping determination
		clearbufshort (floorclip, viewwidth, viewheight);
		// [RH] clip ceiling to console bottom
		clearbufshort (ceilingclip, viewwidth,
			ConBottom > viewwindowy && !bRenderingToCanvas
			? ((ConBottom - viewwindowy) >> detailyshift) : 0);

		lastopening = 0;
	}
}

//==========================================================================
//
// new_visplane
//
// New function, by Lee Killough
// [RH] top and bottom buffers get allocated immediately after the visplane.
//
//==========================================================================

static visplane_t *new_visplane (unsigned hash)
{
	visplane_t *check = freetail;

	if (check == NULL)
	{
		check = (visplane_t *)Calloc (1, sizeof(*check) + sizeof(*check->top)*(MAXWIDTH*2));
		check->bottom = &check->top[MAXWIDTH+2];
	}
	else if (NULL == (freetail = freetail->next))
	{
		freehead = &freetail;
	}

	check->next = visplanes[hash];
	visplanes[hash] = check;
	return check;
}


//==========================================================================
//
// R_FindPlane
//
// killough 2/28/98: Add offsets
//==========================================================================

visplane_t *R_FindPlane (const secplane_t &height, int picnum, int lightlevel,
						 fixed_t xoffs, fixed_t yoffs,
						 fixed_t xscale, fixed_t yscale, angle_t angle,
						 ASkyViewpoint *skybox)
{
	secplane_t plane;
	visplane_t *check;
	unsigned hash;						// killough
	bool isskybox;

	if (picnum == skyflatnum || picnum & PL_SKYFLAT)	// killough 10/98
	{ // most skies map together
		lightlevel = 0;
		xoffs = 0;
		yoffs = 0;
		xscale = 0;
		yscale = 0;
		angle = 0;
		plane.a = plane.b = plane.d = 0;
		// [RH] Map floor skies and ceiling skies to separate visplanes. This isn't
		// always necessary, but it is needed if a floor and ceiling sky are in the
		// same column but separated by a wall. If they both try to reside in the
		// same visplane, then only the floor sky will be drawn.
		plane.c = height.c;
		plane.ic = height.ic;
		isskybox = skybox != NULL && !skybox->bInSkybox &&
			(skybox->bAlways || picnum == skyflatnum);
	}
	else if (skybox != NULL && skybox->bAlways)
	{
		plane = height;
		isskybox = true;
	}
	else
	{
		plane = height;
		isskybox = false;
	}
		
	// New visplane algorithm uses hash table -- killough
	hash = isskybox ? MAXVISPLANES : visplane_hash (picnum, lightlevel, height);

	for (check = visplanes[hash]; check; check = check->next)	// killough
	{
		if (isskybox)
		{
			if (skybox == check->skybox && plane == check->height)
			{
				return check;
			}
		}
		else
		if (plane == check->height &&
			picnum == check->picnum &&
			lightlevel == check->lightlevel &&
			xoffs == check->xoffs &&	// killough 2/28/98: Add offset checks
			yoffs == check->yoffs &&
			basecolormap == check->colormap &&	// [RH] Add more checks
			xscale == check->xscale &&
			yscale == check->yscale &&
			angle == check->angle
			)
		{
		  return check;
		}
	}

	check = new_visplane (hash);		// killough

	check->height = plane;
	check->picnum = picnum;
	check->lightlevel = lightlevel;
	check->xoffs = xoffs;				// killough 2/28/98: Save offsets
	check->yoffs = yoffs;
	check->xscale = xscale;
	check->yscale = yscale;
	check->angle = angle;
	check->colormap = basecolormap;		// [RH] Save colormap
	check->skybox = skybox;
	check->minx = viewwidth;			// Was SCREENWIDTH -- killough 11/98
	check->maxx = -1;

	clearbufshort (check->top, viewwidth, 0x7fff);

	return check;
}

//==========================================================================
//
// R_CheckPlane
//
//==========================================================================

visplane_t *R_CheckPlane (visplane_t *pl, int start, int stop)
{
	int intrl, intrh;
	int unionl, unionh;
	int x;

	if (start < pl->minx)
	{
		intrl = pl->minx;
		unionl = start;
	}
	else
	{
		unionl = pl->minx;
		intrl = start;
	}
		
	if (stop > pl->maxx)
	{
		intrh = pl->maxx;
		unionh = stop;
	}
	else
	{
		unionh = pl->maxx;
		intrh = stop;
	}

	for (x = intrl; x <= intrh && pl->top[x] == 0x7fff; x++)
		;

	if (x > intrh)
	{
		// use the same visplane
		pl->minx = unionl;
		pl->maxx = unionh;
	}
	else
	{
		// make a new visplane
		unsigned hash;

		if (pl->skybox != NULL && !pl->skybox->bInSkybox && (pl->picnum == skyflatnum || pl->skybox->bAlways) && viewactive)
		{
			hash = MAXVISPLANES;
		}
		else
		{
			hash = visplane_hash (pl->picnum, pl->lightlevel, pl->height);
		}
		visplane_t *new_pl = new_visplane (hash);

		new_pl->height = pl->height;
		new_pl->picnum = pl->picnum;
		new_pl->lightlevel = pl->lightlevel;
		new_pl->xoffs = pl->xoffs;			// killough 2/28/98
		new_pl->yoffs = pl->yoffs;
		new_pl->xscale = pl->xscale;		// [RH] copy these, too
		new_pl->yscale = pl->yscale;
		new_pl->angle = pl->angle;
		new_pl->colormap = pl->colormap;
		new_pl->skybox = pl->skybox;
		pl = new_pl;
		pl->minx = start;
		pl->maxx = stop;
		clearbufshort (pl->top, viewwidth, 0x7fff);
	}
	return pl;
}


//==========================================================================
//
// R_MakeSpans
//
//
//==========================================================================

inline void R_MakeSpans (int x, int t1, int b1, int t2, int b2, void (*mapfunc)(int y, int x1))
{
}

//==========================================================================
//
// R_DrawSky
//
// Can handle overlapped skies. Note that the front sky is *not* masked in
// in the normal convention for patches, but uses color 0 as a transparent
// color instead.
//
//==========================================================================

static FTexture *frontskytex, *backskytex;
static angle_t skyflip;
static int frontpos, backpos;
static int frontxscale, backxscale;
static int frontyscale, frontiscale;

extern fixed_t swall[MAXWIDTH];
extern fixed_t lwall[MAXWIDTH];
extern fixed_t rw_offset;
extern FTexture *rw_pic;

// Allow for layer skies up to 512 pixels tall. This is overkill,
// since the most anyone can ever see of the sky is 500 pixels.
// We need 4 skybufs because wallscan can draw up to 4 columns at a time.
static BYTE skybuf[4][512];
static DWORD lastskycol[4];
static int skycolplace;

// Get a column of sky when there is only one sky texture.
static const BYTE *R_GetOneSkyColumn (FTexture *fronttex, int x)
{
	angle_t column = MulScale3 (frontxscale, viewangle + xtoviewangle[x]);

	return fronttex->GetColumn ((((column^skyflip) >> sky1shift) + frontpos) >> FRACBITS, NULL);
}

// Get a column of sky when there are two overlapping sky textures
static const BYTE *R_GetTwoSkyColumns (FTexture *fronttex, int x)
{
	DWORD ang = (viewangle + xtoviewangle[x])^skyflip;
	DWORD angle1 = (((DWORD)MulScale3 (frontxscale, ang) >> sky1shift) + frontpos) >> FRACBITS;
	DWORD angle2 = (((DWORD)MulScale3 (backxscale, ang) >> sky2shift) + backpos) >> FRACBITS;

	// Check if this column has already been built. If so, there's
	// no reason to waste time building it again.
	DWORD skycol = (angle1 << 16) | angle2;
	int i;

	for (i = 0; i < 4; ++i)
	{
		if (lastskycol[i] == skycol)
		{
			return skybuf[i];
		}
	}

	lastskycol[skycolplace] = skycol;
	BYTE *composite = skybuf[skycolplace];
	skycolplace = (skycolplace + 1) & 3;

	// The ordering of the following code has been tuned to allow VC++ to optimize
	// it well. In particular, this arrangement lets it keep count in a register
	// instead of on the stack.
	const BYTE *front = fronttex->GetColumn (angle1, NULL);
	const BYTE *back = backskytex->GetColumn (angle2, NULL);

	int count = MIN<int> (512, MIN (backskytex->GetHeight(), fronttex->GetHeight()));
	i = 0;
	do
	{
		if (front[i])
		{
			composite[i] = front[i];
		}
		else
		{
			composite[i] = back[i];
		}
	} while (++i, --count);
	return composite;
}

static void R_DrawSky (visplane_t *pl)
{
	int x;

	if (pl->minx > pl->maxx)
		return;

	dc_iscale = skyiscale >> skystretch;

	clearbuf (swall+pl->minx, pl->maxx-pl->minx+1, dc_iscale<<2);
	rw_offset = frontpos;

	if (MirrorFlags & RF_XFLIP)
	{
		for (x = pl->minx; x <= pl->maxx; ++x)
		{
			lwall[x] = (viewwidth - x) << FRACBITS;
		}
	}
	else
	{
		for (x = pl->minx; x <= pl->maxx; ++x)
		{
			lwall[x] = x << FRACBITS;
		}
	}

	for (x = 0; x < 4; ++x)
	{
		lastskycol[x] = 0xffffffff;
	}

	rw_pic = frontskytex;
	rw_offset = 0;

	frontxscale = rw_pic->ScaleX ? rw_pic->ScaleX : tx;
	if (backskytex != NULL)
	{
		backxscale = backskytex->ScaleX ? backskytex->ScaleX : tx;
	}

	frontyscale = rw_pic->ScaleY ? rw_pic->ScaleY : ty;
	dc_texturemid = MulScale3 (skytexturemid/*-viewz*/, frontyscale);

	if (1 << frontskytex->HeightBits == frontskytex->GetHeight())
	{ // The texture tiles nicely
		for (x = 0; x < 4; ++x)
		{
			lastskycol[x] = 0xffffffff;
		}
		wallscan (pl->minx, pl->maxx, (short *)pl->top, (short *)pl->bottom, swall, lwall, 
			backskytex == NULL ? R_GetOneSkyColumn : R_GetTwoSkyColumns);
	}
	else
	{ // The texture does not tile nicely
		frontyscale = DivScale3 (skyscale << skystretch, frontyscale);
		frontiscale = DivScale32 (1, frontyscale);
		R_DrawSkyStriped (pl);
	}
}

static void R_DrawSkyStriped (visplane_t *pl)
{
	fixed_t centerysave = centeryfrac;
	short drawheight = (short)MulScale16 (frontskytex->GetHeight(), frontyscale);
	fixed_t topfrac;
	fixed_t iscale = frontiscale;
	short top[MAXWIDTH], bot[MAXWIDTH];
	short yl, yh;
	int x;

	// So that I don't have to worry about fractional precision, chop off the
	// fractional part of centeryfrac.
	centeryfrac = centery << FRACBITS;
	topfrac = (skytexturemid + iscale * (1-centery)) % (frontskytex->GetHeight() << FRACBITS);
	if (topfrac < 0) topfrac += frontskytex->GetHeight() << FRACBITS;
	yl = 0;
	yh = (short)MulScale32 ((frontskytex->GetHeight() << FRACBITS) - topfrac, frontyscale);
	dc_texturemid = topfrac - iscale * (1-centery);

	while (yl < viewheight)
	{
		for (x = pl->minx; x <= pl->maxx; ++x)
		{
			top[x] = MAX (yl, (short)pl->top[x]);
			bot[x] = MIN (yh, (short)pl->bottom[x]);
		}
		for (x = 0; x < 4; ++x)
		{
			lastskycol[x] = 0xffffffff;
		}
		wallscan (pl->minx, pl->maxx, top, bot, swall, lwall, 
			backskytex == NULL ? R_GetOneSkyColumn : R_GetTwoSkyColumns);
		yl = yh;
		yh += drawheight;
		dc_texturemid = iscale * (centery-yl-1);
	}
	centeryfrac = centerysave;
}

//==========================================================================
//
// R_DrawPlanes
//
// At the end of each frame.
//
//==========================================================================

CVAR (Bool, tilt, false, 0);
//CVAR (Int, pa, 0, 0)

void R_DrawPlanes ()
{
	visplane_t *pl;
	int i;
	int vpcount;

	ds_color = 3;

	for (i = vpcount = 0; i < MAXVISPLANES; i++)
	{
		for (pl = visplanes[i]; pl; pl = pl->next)
		{
			vpcount++;
			R_DrawSinglePlane (pl, OPAQUE, false);
		}
	}
}

//==========================================================================
//
// R_DrawSinglePlane
//
// Draws a single visplane.
//
//==========================================================================

void R_DrawSinglePlane (visplane_t *pl, fixed_t alpha, bool masked)
{
//	pl->angle = pa<<ANGLETOFINESHIFT;

	if (pl->minx > pl->maxx)
		return;

	if (r_drawflat)
	{ // [RH] no texture mapping
		ds_color += 4;
		R_MapVisPlane (pl, R_MapColoredPlane);
	}
	else if (pl->picnum == skyflatnum || pl->picnum & PL_SKYFLAT)
	{ // sky flat
		R_DrawSkyPlane (pl);
	}
	else
	{ // regular flat
		FTexture *tex = TexMan(pl->picnum);

		if (tex->UseType == FTexture::TEX_Null)
		{
			return;
		}

		if (!masked)
		{ // If we're not supposed to see through this plane, draw it opaque.
			alpha = OPAQUE;
		}
		else if (!tex->bMasked)
		{ // Don't waste time on a masked texture if it isn't really masked.
			masked = false;
		}
		tex->GetWidth ();
		ds_xbits = tex->WidthBits;
		ds_ybits = tex->HeightBits;
		if ((1 << ds_xbits) > tex->GetWidth())
		{
			ds_xbits--;
		}
		if ((1 << ds_ybits) > tex->GetHeight())
		{
			ds_ybits--;
		}
		pl->xscale = MulScale3 (pl->xscale, tex->ScaleX ? tex->ScaleX : 8);
		pl->yscale = MulScale3 (pl->yscale, tex->ScaleY ? tex->ScaleY : 8);
#ifdef USEASM
		R_SetSpanSize_ASM (ds_xbits, ds_ybits);
#endif
		ds_source = tex->GetPixels ();

		basecolormap = pl->colormap;
		planeshade = LIGHT2SHADE(pl->lightlevel);

		if (r_drawflat || (pl->height.a == 0 && pl->height.b == 0) && !tilt)
		{
			R_DrawNormalPlane (pl, alpha, masked);
		}
		else
		{
			R_DrawTiltedPlane (pl, alpha, masked);
		}
	}
	NetUpdate ();
}

//==========================================================================
//
// R_DrawSkyBoxes
//
// Draws any recorded sky boxes and then frees them.
//
// The process:
//   1. Move the camera to coincide with the SkyViewpoint.
//   2. Clear out the old planes. (They have already been drawn.)
//   3. Clear a window out of the ClipSegs just large enough for the plane.
//   4. Pretend the existing vissprites and drawsegs aren't there.
//   5. Create a drawseg at 0 distance to clip sprites to the visplane. It
//      doesn't need to be associated with a line in the map, since there
//      will never be any sprites in front of it.
//   6. Render the BSP, then planes, then masked stuff.
//   7. Restore the previous vissprites and drawsegs.
//   8. Repeat for any other sky boxes.
//   9. Put the camera back where it was to begin with.
//
//==========================================================================
CVAR (Bool, r_skyboxes, true, 0)
static int numskyboxes;

struct VisplaneAndAlpha
{
	visplane_t *Visplane;
	fixed_t Alpha;
};

void R_DrawSkyBoxes ()
{
	static TArray<size_t> interestingStack;
	static TArray<ptrdiff_t> drawsegStack;
	static TArray<ptrdiff_t> visspriteStack;
	static TArray<fixed_t> viewxStack, viewyStack, viewzStack;
	static TArray<VisplaneAndAlpha> visplaneStack;

	if (visplanes[MAXVISPLANES] == NULL)
		return;

	VisplaneAndAlpha vaAdder;
	int savedextralight = extralight;
	fixed_t savedx = viewx;
	fixed_t savedy = viewy;
	fixed_t savedz = viewz;
	angle_t savedangle = viewangle;
	ptrdiff_t savedvissprite_p = vissprite_p - vissprites;
	ptrdiff_t savedds_p = ds_p - drawsegs;
	ptrdiff_t savedlastopening = lastopening;
	unsigned int savedinteresting = FirstInterestingDrawseg;
	float savedvisibility = R_GetVisibility ();
	AActor *savedcamera = camera;
	sector_t *savedsector = viewsector;

	int i;
	visplane_t *pl;

	numskyboxes = 0;

	for (pl = visplanes[MAXVISPLANES]; pl != NULL; pl = visplanes[MAXVISPLANES])
	{
		// Pop the visplane off the list now so that if this skybox adds more
		// skyboxes to the list, they will be drawn instead of skipped (because
		// new skyboxes go to the beginning of the list instead of the end).
		visplanes[MAXVISPLANES] = pl->next;
		pl->next = NULL;

		if (pl->maxx < pl->minx || !r_skyboxes)
		{
			R_DrawSinglePlane (pl, OPAQUE, false);
			*freehead = pl;
			freehead = &pl->next;
			continue;
		}

		numskyboxes++;

		ASkyViewpoint *sky = pl->skybox;

		if (sky->Mate == NULL)
		{
			// Don't let gun flashes brighten the sky box
			extralight = 0;
			R_SetVisibility (sky->args[0] * 0.25f);
			viewx = sky->x;
			viewy = sky->y;
			viewz = sky->z;
			viewangle = savedangle + sky->angle;
		}
		else
		{
			extralight = savedextralight;
			R_SetVisibility (savedvisibility);
			viewx = savedx - sky->Mate->x + sky->x;
			viewy = savedy - sky->Mate->y + sky->y;
			viewz = savedz;
			viewangle = savedangle;
		}

		sky->bInSkybox = true;
		camera = sky;
		viewsector = sky->Sector;
		R_SetViewAngle ();
		validcount++;	// Make sure we see all sprites

		R_ClearPlanes (false);
		R_ClearClipSegs (pl->minx, pl->maxx + 1);
		WindowLeft = pl->minx;
		WindowRight = pl->maxx;

		for (i = pl->minx; i <= pl->maxx; i++)
		{
			if (pl->top[i] == 0x7fff)
			{
				ceilingclip[i] = viewheight;
				floorclip[i] = -1;
			}
			else
			{
				ceilingclip[i] = pl->top[i];
				floorclip[i] = pl->bottom[i];
			}
		}

		// Create a drawseg to clip sprites to the sky plane
		R_CheckDrawSegs ();
		R_CheckOpenings ((pl->maxx - pl->minx + 1)*2);
		ds_p->siz1 = INT_MAX;
		ds_p->siz2 = INT_MAX;
		ds_p->sz1 = 0;
		ds_p->sz2 = 0;
		ds_p->x1 = pl->minx;
		ds_p->x2 = pl->maxx;
		ds_p->silhouette = SIL_BOTH;
		ds_p->sprbottomclip = R_NewOpening (pl->maxx - pl->minx + 1);
		ds_p->sprtopclip = R_NewOpening (pl->maxx - pl->minx + 1);
		ds_p->maskedtexturecol = ds_p->swall = -1;
		ds_p->bFogBoundary = false;
		memcpy (openings + ds_p->sprbottomclip, floorclip + pl->minx, (pl->maxx - pl->minx + 1)*sizeof(short));
		memcpy (openings + ds_p->sprtopclip, ceilingclip + pl->minx, (pl->maxx - pl->minx + 1)*sizeof(short));

		firstvissprite = vissprite_p;
		firstdrawseg = ds_p++;
		FirstInterestingDrawseg = InterestingDrawsegs.Size();

		interestingStack.Push (FirstInterestingDrawseg);
		drawsegStack.Push (firstdrawseg - drawsegs);
		visspriteStack.Push (firstvissprite - vissprites);
		viewxStack.Push (viewx);
		viewyStack.Push (viewy);
		viewzStack.Push (viewz);
		vaAdder.Visplane = pl;
		vaAdder.Alpha = sky->PlaneAlpha;
		visplaneStack.Push (vaAdder);

		R_RenderBSPNode (nodes + numnodes - 1);
		R_DrawPlanes ();

		sky->bInSkybox = false;
	}

	// Draw all the masked textures in a second pass, in the reverse order they
	// were added. This must be done separately from the previous step for the
	// sake of nested skyboxes.
	while (interestingStack.Pop (FirstInterestingDrawseg))
	{
		ptrdiff_t pd;

		drawsegStack.Pop (pd);
		firstdrawseg = drawsegs + pd;
		visspriteStack.Pop (pd);
		firstvissprite = vissprites + pd;
		viewxStack.Pop (viewx);	// Masked textures and planes need the view
		viewyStack.Pop (viewy); // coordinates restored for proper positioning.
		viewzStack.Pop (viewz);

		R_DrawMasked ();

		ds_p = firstdrawseg;
		vissprite_p = firstvissprite;

		visplaneStack.Pop (vaAdder);
		if (vaAdder.Alpha > 0)
		{
			R_DrawSinglePlane (vaAdder.Visplane, vaAdder.Alpha, true);
		}
		*freehead = vaAdder.Visplane;
		freehead = &vaAdder.Visplane->next;
	}
	firstvissprite = vissprites;
	vissprite_p = vissprites + savedvissprite_p;
	firstdrawseg = drawsegs;
	ds_p = drawsegs + savedds_p;
	InterestingDrawsegs.Resize (FirstInterestingDrawseg);
	FirstInterestingDrawseg = savedinteresting;

	lastopening = savedlastopening;

	camera = savedcamera;
	viewsector = savedsector;
	viewx = savedx;
	viewy = savedy;
	viewz = savedz;
	R_SetVisibility (savedvisibility);
	extralight = savedextralight;
	viewangle = savedangle;
	R_SetViewAngle ();

	for (*freehead = visplanes[MAXVISPLANES], visplanes[MAXVISPLANES] = NULL; *freehead; )
		freehead = &(*freehead)->next;
}

ADD_STAT(skyboxes, out)
{
	sprintf (out, "%d skybox planes", numskyboxes);
}

//==========================================================================
//
// R_DrawSkyPlane
//
//==========================================================================

void R_DrawSkyPlane (visplane_t *pl)
{
	int sky1tex, sky2tex;

	if ((level.flags & LEVEL_SWAPSKIES) && !(level.flags & LEVEL_DOUBLESKY))
	{
		sky1tex = sky2texture;
	}
	else
	{
		sky1tex = sky1texture;
	}
	sky2tex = sky2texture;

	if (pl->picnum == skyflatnum)
	{	// use sky1
sky1:
		frontskytex = TexMan(sky1tex);
		if (level.flags & LEVEL_DOUBLESKY)
			backskytex = TexMan(sky2tex);
		else
			backskytex = NULL;
		skyflip = 0;
		frontpos = sky1pos;
		backpos = sky2pos;
	}
	else if (pl->picnum == PL_SKYFLAT)
	{	// use sky2
		frontskytex = TexMan(sky2tex);
		backskytex = NULL;
		skyflip = 0;
		frontpos = sky2pos;
	}
	else
	{	// MBF's linedef-controlled skies
		// Sky Linedef
		const line_t *l = &lines[(pl->picnum & ~PL_SKYFLAT)-1];

		// Sky transferred from first sidedef
		const side_t *s = *l->sidenum + sides;

		// Texture comes from upper texture of reference sidedef
		// [RH] If swapping skies, then use the lower sidedef
		if (level.flags & LEVEL_SWAPSKIES && s->bottomtexture != 0)
		{
			frontskytex = TexMan(s->bottomtexture);
		}
		else
		{
			frontskytex = TexMan(s->toptexture);
		}
		if (frontskytex->UseType == FTexture::TEX_Null)
		{ // [RH] The blank texture: Use normal sky instead.
			goto sky1;
		}
		backskytex = NULL;

		// Horizontal offset is turned into an angle offset,
		// to allow sky rotation as well as careful positioning.
		// However, the offset is scaled very small, so that it
		// allows a long-period of sky rotation.
		frontpos = (-s->textureoffset) >> 6;

		// Vertical offset allows careful sky positioning.
		dc_texturemid = s->rowoffset - 28*FRACUNIT;

		// We sometimes flip the picture horizontally.
		//
		// Doom always flipped the picture, so we make it optional,
		// to make it easier to use the new feature, while to still
		// allow old sky textures to be used.
		skyflip = l->args[2] ? 0u : ~0u;
	}

	bool fakefixed = false;
	if (fixedcolormap)
	{
		dc_colormap = fixedcolormap;
	}
	else if (!fixedcolormap)
	{
		fakefixed = true;
		fixedcolormap = dc_colormap = NormalLight.Maps;
	}

	R_DrawSky (pl);

	if (fakefixed)
		fixedcolormap = NULL;
}

//==========================================================================
//
// R_DrawNormalPlane
//
//==========================================================================

void R_DrawNormalPlane (visplane_t *pl, fixed_t alpha, bool masked)
{
#ifdef USEASM
	if (ds_source != ds_cursource)
	{
		R_SetSpanSource_ASM (ds_source);
	}
#endif

	if (alpha <= 0)
	{
		return;
	}

	angle_t planeang = pl->angle;
	xscale = pl->xscale << (16 - ds_xbits);
	yscale = pl->yscale << (16 - ds_ybits);
	if (planeang != 0)
	{
		fixed_t cosine = finecosine[planeang >> ANGLETOFINESHIFT];
		fixed_t sine = finesine[planeang >> ANGLETOFINESHIFT];

		pviewx = pl->xoffs + FixedMul (viewx, cosine) - FixedMul (viewy, sine);
		pviewy = pl->yoffs - FixedMul (viewx, sine) - FixedMul (viewy, cosine);
	}
	else
	{
		pviewx = pl->xoffs + viewx;
		pviewy = pl->yoffs - viewy;
	}

	pviewx = FixedMul (xscale, pviewx);
	pviewy = FixedMul (yscale, pviewy);
	
	// left to right mapping
	planeang = (viewangle - ANG90 + planeang) >> ANGLETOFINESHIFT;
	// Scale will be unit scale at FocalLengthX (normally SCREENWIDTH/2) distance
	xstepscale = Scale (xscale, finecosine[planeang], FocalLengthX);
	ystepscale = Scale (yscale, -finesine[planeang], FocalLengthX);

	// [RH] flip for mirrors
	if (MirrorFlags & RF_XFLIP)
	{
		xstepscale = (DWORD)(-(SDWORD)xstepscale);
		ystepscale = (DWORD)(-(SDWORD)ystepscale);
	}

	int x = pl->maxx - halfviewwidth;
	planeang = (planeang + (ANG90 >> ANGLETOFINESHIFT)) & FINEMASK;
	basexfrac = FixedMul (xscale, finecosine[planeang]) + x*xstepscale;
	baseyfrac = FixedMul (yscale, -finesine[planeang]) + x*ystepscale;

	planeheight = abs (FixedMul (pl->height.d, -pl->height.ic) - viewz);

	GlobVis = FixedDiv (r_FloorVisibility, planeheight);
	if (fixedlightlev)
		ds_colormap = basecolormap + fixedlightlev, plane_shade = false;
	else if (fixedcolormap)
		ds_colormap = fixedcolormap, plane_shade = false;
	else
		plane_shade = true;

	if (spanfunc != R_FillSpan)
	{
		if (masked)
		{
			if (alpha < OPAQUE)
			{
				spanfunc = R_DrawSpanMaskedTranslucent;
				dc_srcblend = Col2RGB8[alpha>>10];
				dc_destblend = Col2RGB8[(OPAQUE-alpha)>>10];
			}
			else
			{
				spanfunc = R_DrawSpanMasked;
			}
		}
		else
		{
			if (alpha < OPAQUE)
			{
				spanfunc = R_DrawSpanTranslucent;
				dc_srcblend = Col2RGB8[alpha>>10];
				dc_destblend = Col2RGB8[(OPAQUE-alpha)>>10];
			}
			else
			{
				spanfunc = R_DrawSpan;
			}
		}
	}
	R_MapVisPlane (pl, R_MapPlane);
}

//==========================================================================
//
// R_DrawTiltedPlane
//
//==========================================================================

void R_DrawTiltedPlane (visplane_t *pl, fixed_t alpha, bool masked)
{
	static const float ifloatpow2[16] =
	{
		// ifloatpow2[i] = 1 / (1 << i)
		64.f, 32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f,
		0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f,
		0.00390625f, 0.001953125f
		/*, 0.0009765625f, 0.00048828125f, 0.000244140625f,
		1.220703125e-4f, 6.103515625e-5, 3.0517578125e-5*/
	};
	float lxscale, lyscale;
	float xscale, yscale;
	fixed_t ixscale, iyscale;
	angle_t ang;
	vec3_t p, m, n;
	fixed_t zeroheight;

	if (alpha <= 0)
	{
		return;
	}

	// p is the texture origin in view space
	// Don't add in the offsets at this stage, because doing so can result in
	// errors if the flat is rotated.

	lxscale = FIXED2FLOAT(pl->xscale) * ifloatpow2[ds_xbits];
	lyscale = FIXED2FLOAT(pl->yscale) * ifloatpow2[ds_ybits];
	xscale = 64.f / lxscale;
	yscale = 64.f / lyscale;
	ixscale = quickertoint(xscale*65536.f);
	iyscale = quickertoint(yscale*65536.f);
	zeroheight = pl->height.ZatPoint (viewx, viewy);

	pviewx = MulScale (pl->xoffs, pl->xscale, ds_xbits);
	pviewy = MulScale (pl->yoffs, pl->yscale, ds_ybits);

	ang = (ANG270 - viewangle) >> ANGLETOFINESHIFT;
	p[0] = FIXED2FLOAT(DMulScale16 (viewx, finecosine[ang], -viewy, finesine[ang]));
	p[2] = FIXED2FLOAT(DMulScale16 (viewx, finesine[ang], viewy, finecosine[ang]));
	p[1] = FIXED2FLOAT(pl->height.ZatPoint (0, 0) - viewz);

	// m is the v direction vector in view space
	ang = (ANG180 - viewangle - pl->angle) >> ANGLETOFINESHIFT;
	m[0] = yscale * FIXED2FLOAT(finecosine[ang]);
	m[2] = yscale * FIXED2FLOAT(finesine[ang]);
//	m[1] = FIXED2FLOAT(pl->height.ZatPoint (0, iyscale) - pl->height.ZatPoint (0,0));
//	VectorScale2 (m, 64.f/VectorLength(m));

	// n is the u direction vector in view space
	ang = (ang + (ANG90>>ANGLETOFINESHIFT)) & FINEMASK;
	n[0] = -xscale * FIXED2FLOAT(finecosine[ang]);
	n[2] = -xscale * FIXED2FLOAT(finesine[ang]);
//	n[1] = FIXED2FLOAT(pl->height.ZatPoint (ixscale, 0) - pl->height.ZatPoint (0,0));
//	VectorScale2 (n, 64.f/VectorLength(n));

	ang = pl->angle >> ANGLETOFINESHIFT;
	m[1] = FIXED2FLOAT(pl->height.ZatPoint (
		viewx + MulScale16 (iyscale, finesine[ang]),
		viewy + MulScale16 (iyscale, finecosine[ang])) - zeroheight);
	ang = (pl->angle + ANGLE_90) >> ANGLETOFINESHIFT;
	n[1] = FIXED2FLOAT(pl->height.ZatPoint (
		viewx + MulScale16 (ixscale, finesine[ang]),
		viewy + MulScale16 (ixscale, finecosine[ang])) - zeroheight);

	CrossProduct (p, m, plane_su);
	CrossProduct (p, n, plane_sv);
	CrossProduct (m, n, plane_sz);

	plane_su[2] *= FocalLengthXfloat;
	plane_sv[2] *= FocalLengthXfloat;
	plane_sz[2] *= FocalLengthXfloat;

	plane_su[1] *= iyaspectmulfloat;
	plane_sv[1] *= iyaspectmulfloat;
	plane_sz[1] *= iyaspectmulfloat;

	// Premultiply the texture vectors with the scale factors
	VectorScale2 (plane_su, 4294967296.f);
	VectorScale2 (plane_sv, 4294967296.f);

	if (MirrorFlags & RF_XFLIP)
	{
		plane_su[0] = -plane_su[0];
		plane_sv[0] = -plane_sv[0];
		plane_sz[0] = -plane_sz[0];
	}

	planelightfloat = (r_TiltVisibility * lxscale * lyscale) / (float)(abs(pl->height.ZatPoint (viewx, viewy) - viewz));

	if (pl->height.c > 0)
		planelightfloat = -planelightfloat;

	if (fixedlightlev)
		ds_colormap = basecolormap + fixedlightlev, plane_shade = false;
	else if (fixedcolormap)
		ds_colormap = fixedcolormap, plane_shade = false;
	else
		ds_colormap = basecolormap, plane_shade = true;

	if (!plane_shade)
	{
		for (int i = 0; i < viewwidth; ++i)
		{
			tiltlighting[i] = ds_colormap;
		}
	}

#if defined(USEASM)
	if (ds_source != ds_curtiltedsource)
		R_SetTiltedSpanSource_ASM (ds_source);
	R_MapVisPlane (pl, R_DrawTiltedPlane_ASM);
#else
	R_MapVisPlane (pl, R_MapTiltedPlane);
#endif
}

//==========================================================================
//
// R_MapVisPlane
//
// t1/b1 are at x
// t2/b2 are at x+1
// spanend[y] is at the right edge
//
//==========================================================================

void R_MapVisPlane (visplane_t *pl, void (*mapfunc)(int y, int x1))
{
	int x = pl->maxx;
	int t2 = pl->top[x];
	int b2 = pl->bottom[x];

	if (b2 > t2)
	{
		clearbufshort (spanend+t2, b2-t2, x);
	}

	for (--x; x >= pl->minx; --x)
	{
		int t1 = pl->top[x];
		int b1 = pl->bottom[x];
		const int xr = x+1;
		int stop;

		// Draw any spans that have just closed
		stop = MIN (t1, b2);
		while (t2 < stop)
		{
			mapfunc (t2++, xr);
		}
		stop = MAX (b1, t2);
		while (b2 > stop)
		{
			mapfunc (--b2, xr);
		}

		// Mark any spans that have just opened
		stop = MIN (t2, b1);
		while (t1 < stop)
		{
			spanend[t1++] = x;
		}
		stop = MAX (b2, t2);
		while (b1 > stop)
		{
			spanend[--b1] = x;
		}

		t2 = pl->top[x];
		b2 = pl->bottom[x];
		basexfrac -= xstepscale;
		baseyfrac -= ystepscale;
	}
	// Draw any spans that are still open
	while (t2 < b2)
	{
		mapfunc (--b2, pl->minx);
	}
}

//==========================================================================
//
// R_PlaneInitData
//
//==========================================================================

BOOL R_PlaneInitData ()
{
	int i;
	visplane_t *pl;

	// Free all visplanes and let them be re-allocated as needed.
	pl = freetail;

	while (pl)
	{
		visplane_t *next = pl->next;
		free (pl);
		pl = next;
	}
	freetail = NULL;
	freehead = &freetail;

	for (i = 0; i < MAXVISPLANES; i++)
	{
		pl = visplanes[i];
		visplanes[i] = NULL;
		while (pl)
		{
			visplane_t *next = pl->next;
			free (pl);
			pl = next;
		}
	}

	return true;
}

//==========================================================================
//
// R_AlignFlat
//
//==========================================================================

bool R_AlignFlat (int linenum, int side, int fc)
{
	line_t *line = lines + linenum;
	sector_t *sec = side ? line->backsector : line->frontsector;

	if (!sec)
		return false;

	fixed_t x = line->v1->x;
	fixed_t y = line->v1->y;

	angle_t angle = R_PointToAngle2 (x, y, line->v2->x, line->v2->y);
	angle_t norm = (angle-ANGLE_90) >> ANGLETOFINESHIFT;

	fixed_t dist = -FixedMul (finecosine[norm], x) - FixedMul (finesine[norm], y);

	if (side)
	{
		angle = angle + ANGLE_180;
		dist = -dist;
	}

	if (fc)
	{
		sec->base_ceiling_angle = 0-angle;
		sec->base_ceiling_yoffs = dist & ((1<<(FRACBITS+8))-1);
	}
	else
	{
		sec->base_floor_angle = 0-angle;
		sec->base_floor_yoffs = dist & ((1<<(FRACBITS+8))-1);
	}

	return true;
}
