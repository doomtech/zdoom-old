
//**************************************************************************
//**
//** w_wad.c : Heretic 2 : Raven Software, Corp.
//**
//** $RCSfile: w_wad.c,v $
//** $Revision: 1.6 $
//** $Date: 95/10/06 20:56:47 $
//** $Author: cjr $
//**
//**************************************************************************

// HEADER FILES ------------------------------------------------------------

#include <stdlib.h>
#ifndef UNIX
#include <io.h>
#endif
#include <ctype.h>
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "m_alloc.h"
#include "doomtype.h"
#include "doomstat.h"
#include "doomdef.h"
#include "m_swap.h"
#include "m_argv.h"
#include "i_system.h"
#include "z_zone.h"
#include "cmdlib.h"
#include "w_wad.h"

// MACROS ------------------------------------------------------------------

#ifdef NeXT
// NeXT doesn't need a binary flag in open call
#define O_BINARY 0
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define MAX_PERM_OPEN_WADS	2	// Max # of wads to always keep open
#define MAX_OPEN_WADS		16	// Max # of additional wads to have open at a time

#define NULL_INDEX		(0xffff)

// TYPES -------------------------------------------------------------------

struct FWadFileHandle
{
	char *Name;
	int Handle;
	int FirstLump;
	int LastLump;
	int ActivePos;
};

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

static void W_SkinHack (int baselump);
static void PutWadToFront (int wadnum, int oldpos);

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

lumpinfo_t *lumpinfo;
int numlumps;
void **lumpcache;
WORD *FirstLumpIndex, *NextLumpIndex;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static TArray<FWadFileHandle> ActiveWads (MAX_OPEN_WADS);
static int OpenWads[MAX_OPEN_WADS];
static int NumOpenWads;

// CODE --------------------------------------------------------------------

//==========================================================================
//
// strupr
//
//==========================================================================

#ifdef NO_STRUPR
void strupr (char *s)
{
    while (*s)
	*s++ = toupper (*s);
}
#endif

//==========================================================================
//
// filelength
//
//==========================================================================

#ifdef NO_FILELENGTH
int filelength (int handle)
{
    struct stat fileinfo;

    if (fstat (handle, &fileinfo) == -1)
	{
		close (handle);
		I_Error ("Error fstating");
	}
    return fileinfo.st_size;
}
#endif

//==========================================================================
//
// uppercoppy
//
// [RH] Copy up to 8 chars, upper-casing them in the process
//==========================================================================

void uppercopy (char *to, const char *from)
{
	int i;

	for (i = 0; i < 8 && from[i]; i++)
		to[i] = toupper (from[i]);
	for (; i < 8; i++)
		to[i] = 0;
}

//==========================================================================
//
// W_AddFile
//
// Files with a .wad extension are wadlink files with multiple lumps,
// other files are single lumps with the base filename for the lump name.
//
// [RH] Removed reload hack
//==========================================================================

void W_AddFile (char *filename)
{
	FWadFileHandle	wadhandle;
	char			name[256];
	wadinfo_t		header;
	lumpinfo_t*		lump_p;
	unsigned		i;
	int				handle;
	int				length;
	int				startlump;
	filelump_t*		fileinfo, *fileinfo2free;
	filelump_t		singleinfo;

	// [RH] Automatically append .wad extension if none is specified.
	FixPathSeperator (filename);
	strcpy (name, filename);
	DefaultExtension (name, ".wad");

	if (NumOpenWads >= MAX_OPEN_WADS)
	{
		NumOpenWads--;
		close (ActiveWads[OpenWads[NumOpenWads]].Handle);
		ActiveWads[OpenWads[NumOpenWads]].Handle = -1;
	}

	// open the file and add to directory
	if ((handle = open (name, O_RDONLY|O_BINARY)) == -1)
	{ // Didn't find file
		Printf (PRINT_HIGH, " couldn't open %s\n",filename);
		return;
	}

	Printf (PRINT_HIGH, " adding %s", name);
	startlump = numlumps;

	// [RH] Determine if file is a WAD based on its signature, not its name.
	read (handle, &header, sizeof(header));

	if (header.identification == IWAD_ID || header.identification == PWAD_ID)
	{ // This is a WAD file

		header.numlumps = LONG(header.numlumps);
		header.infotableofs = LONG(header.infotableofs);
		length = header.numlumps * sizeof(filelump_t);
		fileinfo = fileinfo2free = new filelump_t[header.numlumps];
		lseek (handle, header.infotableofs, SEEK_SET);
		read (handle, fileinfo, length);
		numlumps += header.numlumps;
		Printf (PRINT_HIGH, " (%d lumps)", header.numlumps);
	}
	else
	{ // This is just a single lump file

		fileinfo2free = NULL;
		fileinfo = &singleinfo;
		singleinfo.filepos = 0;
		singleinfo.size = LONG(filelength(handle));
		ExtractFileBase (filename, name);
		strupr (name);
		strncpy (singleinfo.name, name, 8);
		numlumps++;
	}
	Printf (PRINT_HIGH, "\n");

	// Fill in lumpinfo
	lumpinfo = (lumpinfo_t *)Realloc (lumpinfo, numlumps*sizeof(lumpinfo_t));
	lump_p = &lumpinfo[startlump];
	for (i = startlump; i < numlumps; i++, lump_p++, fileinfo++)
	{
		lump_p->handle = ActiveWads.Size ();
		lump_p->position = LONG(fileinfo->filepos);
		lump_p->size = LONG(fileinfo->size);
		lump_p->namespc = ns_global;
		// [RH] Convert name to uppercase during copy
		uppercopy (lump_p->name, fileinfo->name);
	}

	if (fileinfo2free)
		delete[] fileinfo2free;

	wadhandle.Name = copystring (name);
	wadhandle.Handle = handle;
	wadhandle.FirstLump = startlump;
	wadhandle.LastLump = numlumps - 1;

	if (ActiveWads.Push (wadhandle) >= MAX_PERM_OPEN_WADS)
	{
		PutWadToFront (ActiveWads.Size () - 1, NumOpenWads);
		NumOpenWads++;
	}
}

//==========================================================================
//
// W_InitMultipleFiles
//
// Pass a null terminated list of files to use. All files are optional,
// but at least one file must be found. Lump names can appear multiple
// times. The name searcher looks backwards, so a later file can
// override an earlier one.
//
//==========================================================================

void W_InitMultipleFiles (wadlist_t **filenames)
{
	int i;
	int numfiles;

	// open all the files, load headers, and count lumps
	numfiles = 0;
	numlumps = 0;
	lumpinfo = NULL; // will be realloced as lumps are added

	while (*filenames)
	{
		wadlist_t *next = (*filenames)->next;
		int baselump = numlumps;

		W_AddFile ((*filenames)->name);
		Z_Free (*filenames);
		*filenames = next;

		// The first two files are always zdoom.wad and the IWAD, which
		// do not contain skins.
		if (++numfiles > 2)
			W_SkinHack (baselump);
	}

	if (!numlumps)
	{
		I_FatalError ("W_InitMultipleFiles: no files found");
	}

	// [RH] Merge sprite and flat groups.
	//		(We don't need to bother with patches, since
	//		Doom doesn't use markers to identify them.)
	W_MergeLumps ("S_START", "S_END", ns_sprites);
	W_MergeLumps ("F_START", "F_END", ns_flats);
	W_MergeLumps ("C_START", "C_END", ns_colormaps);

	// [RH] Set up hash table
	FirstLumpIndex = new WORD[numlumps];
	NextLumpIndex = new WORD[numlumps];
	W_InitHashChains ();

	// set up caching
	i = numlumps * sizeof(*lumpcache);
	lumpcache = new void *[numlumps];
	memset (lumpcache, 0, i);
}

//===========================================================================
//
// W_InitFile
//
// Initialize the primary from a single file.
//
//==========================================================================

void W_InitFile (char *filename)
{
	wadlist_t *names = (wadlist_t *)Z_Malloc (sizeof(*names)+strlen(filename), PU_STATIC, 0);

	names->next = NULL;
	strcpy (names->name, filename);
	W_InitMultipleFiles (&names);
}

//==========================================================================
//
// PutWadToFront
//
// Marks the indicated wad as most-recently-used.
//
//==========================================================================

static void PutWadToFront (int wadnum, int oldpos)
{
	int i;

	for (i = oldpos; i > 0; i--)
	{
		OpenWads[i] = OpenWads[i-1];
		ActiveWads[OpenWads[i]].ActivePos = i;
	}
	OpenWads[i] = wadnum;
	ActiveWads[wadnum].ActivePos = 0;
}

//==========================================================================
//
// FileHandleFromWad
//
// Returns a file handle to access the specified wad with. If the wad was
// not open, it gets opened and added to the list of open wads, possibly
// closing the oldest wad on the list. Thus, handles returned by this
// function are not guaranteed to stay valid across multiple calls to this
// function.
//
//==========================================================================

int W_FileHandleFromWad (int wadnum)
{
	if ((unsigned int) wadnum >= ActiveWads.Size ())
	{
		return -1;
	}
	if (ActiveWads[wadnum].Handle == -1)
	{
		if (NumOpenWads >= MAX_OPEN_WADS)
		{
			close (ActiveWads[OpenWads[MAX_OPEN_WADS-1]].Handle);
			ActiveWads[OpenWads[MAX_OPEN_WADS-1]].Handle = -1;
			NumOpenWads--;
		}
		if ((ActiveWads[wadnum].Handle = open (ActiveWads[wadnum].Name,
			O_RDONLY|O_BINARY)) == -1)
		{
			I_FatalError ("Could not reopen %s\n", ActiveWads[wadnum].Name);
		}
		PutWadToFront (wadnum, NumOpenWads);
		NumOpenWads++;
	}
	else if (wadnum >= MAX_PERM_OPEN_WADS)
	{
		PutWadToFront (wadnum, ActiveWads[wadnum].ActivePos);
	}
	return ActiveWads[wadnum].Handle;
}

//==========================================================================
//
// W_NumLumps
//
//==========================================================================

int W_NumLumps (void)
{
	return numlumps;
}

//==========================================================================
//
// W_CheckNumForName
//
// Returns -1 if name not found.
//
// [RH] Changed to use hash lookup ala BOOM instead of a linear search
// and namespace parameter
//==========================================================================

int W_CheckNumForName (const char *name, int space)
{
	char uname[8];
	WORD i;

	uppercopy (uname, name);
	i = FirstLumpIndex[W_LumpNameHash (uname) % (unsigned)numlumps];

	while (i != NULL_INDEX &&
		(*(__int64 *)&lumpinfo[i].name != *(__int64 *)&uname ||
		 lumpinfo[i].namespc != space))
	{
		i = NextLumpIndex[i];
	}

	return i != NULL_INDEX ? i : -1;
}

//==========================================================================
//
// W_GetNumForName
//
// Calls W_CheckNumForName, but bombs out if not found.
//
//==========================================================================

int W_GetNumForName (const char *name)
{
	int	i;

	i = W_CheckNumForName (name);

	if (i == -1)
		I_Error ("W_GetNumForName: %s not found!", name);

	return i;
}


//==========================================================================
//
// W_LumpLength
//
// Returns the buffer size needed to load the given lump.
//
//==========================================================================

int W_LumpLength (int lump)
{
	if (lump >= numlumps)
		I_Error ("W_LumpLength: %i >= numlumps",lump);

	return lumpinfo[lump].size;
}

//==========================================================================
//
// W_ReadLump
//
// Loads the lump into the given buffer, which must be >= W_LumpLength().
// R
//==========================================================================

void W_ReadLump (int lump, void *dest)
{
	int c, handle;
	lumpinfo_t *l;
	
	if (lump >= numlumps)
	{
		I_Error ("W_ReadLump: %i >= numlumps",lump);
	}
	l = lumpinfo + lump;
	handle = W_FileHandleFromWad (l->handle);
	lseek (handle, l->position, SEEK_SET);
	c = read (handle, dest, l->size);
	if (c < l->size)
	{
		I_Error ("W_ReadLump: only read %i of %i on lump %i\n(%s)",
			c, l->size, lump, strerror(errno));	
	}
}

//==========================================================================
//
// W_CacheLumpNum
//
//==========================================================================

void *W_CacheLumpNum (int lump, int tag)
{
	byte *ptr;
	int lumplen;

	if ((unsigned)lump >= numlumps)
	{
		I_Error ("W_CacheLumpNum: %u >= numlumps",lump);
	}
	if (!lumpcache[lump])
	{
		// read the lump in
		//DPrintf ("cache miss on lump %i\n", lump);
		// [RH] Allocate one byte more than necessary for the
		//		lump and set the extra byte to zero so that
		//		various text parsing routines can just call
		//		W_CacheLumpNum() and not choke.
		lumplen = W_LumpLength (lump);
		ptr = (byte *)Z_Malloc (lumplen + 1, tag, &lumpcache[lump]);
		W_ReadLump (lump, lumpcache[lump]);
		ptr[lumplen] = 0;
	}
	else
	{
		//DPrintf ("cache hit on lump %i\n",lump);
		Z_ChangeTagSafe (lumpcache[lump], tag);
	}
	return lumpcache[lump];
}

//==========================================================================
//
// W_LumpNameHash
//
// [RH] This is from Boom.
//		NOTE: s should already be uppercase.
//		This is different from the BOOM version.
//
// Hash function used for lump names.
// Must be mod'ed with table size.
// Can be used for any 8-character names.
// by Lee Killough
//
//==========================================================================

unsigned W_LumpNameHash (const char *s)
{
	unsigned hash;

	(void) ((hash =			 s[0], s[1]) &&
			(hash = hash*3 + s[1], s[2]) &&
			(hash = hash*2 + s[2], s[3]) &&
			(hash = hash*2 + s[3], s[4]) &&
			(hash = hash*2 + s[4], s[5]) &&
			(hash = hash*2 + s[5], s[6]) &&
			(hash = hash*2 + s[6],
			 hash = hash*2 + s[7])
			);
	return hash;
}

//==========================================================================
//
// W_InitHashChains
//
// Prepares the lumpinfos for hashing.
// (Hey! This looks suspiciously like something from Boom! :-)
//
//==========================================================================

void W_InitHashChains (void)
{
	char name[8];
	unsigned int i, j;

	// Mark all buckets as empty
	memset (FirstLumpIndex, 255, numlumps*sizeof(FirstLumpIndex[0]));
	memset (NextLumpIndex, 255, numlumps*sizeof(FirstLumpIndex[0]));

	// Now set up the chains
	for (i = 0; i < numlumps; i++)
	{
		uppercopy (name, lumpinfo[i].name);
		j = W_LumpNameHash (name) % (unsigned) numlumps;
		NextLumpIndex[i] = FirstLumpIndex[j];
		FirstLumpIndex[j] = i;
	}
}

//==========================================================================
//
// IsMarker
//
// (from BOOM)
//
//==========================================================================

static BOOL IsMarker (const lumpinfo_t *lump, const char *marker)
{
	return (lump->namespc == ns_global) && (!strncmp (lump->name, marker, 8) || 
			(*(lump->name) == *marker && !strncmp (lump->name + 1, marker, 7)));
}

//==========================================================================
//
// W_MergeLumps
//
// Merge multiple tagged groups into one
// Basically from BOOM, too, although I tried to write it independently.
//
//==========================================================================

void W_MergeLumps (const char *start, const char *end, int space)
{
	char ustart[8], uend[8];
	lumpinfo_t *newlumpinfos;
	int newlumps, oldlumps, i;
	BOOL insideBlock;
	int flatHack;

	uppercopy (ustart, start);
	uppercopy (uend, end);

	// Some pwads use an icky hack to get extra flats with regular Doom.
	// This tries to detect them.
	flatHack = 0;
	if (strcmp ("F_START", ustart) == 0 && !Args.CheckParm ("-noflathack"))
	{
		int fudge = 0, start = 0;

		for (i = 0; i < numlumps; i++)
		{
			if (IsMarker (lumpinfo + i, ustart))
				fudge++, start = i;
			else if (IsMarker (lumpinfo + i, uend))
				fudge--, flatHack = i;
		}
		if (start > flatHack)
			fudge--;
		if (fudge >= 0)
			flatHack = 0;
	}

	newlumpinfos = new lumpinfo_t[numlumps];

	newlumps = 0;
	oldlumps = 0;
	insideBlock = false;

	for (i = 0; i < numlumps; i++)
	{
		if (!insideBlock)
		{
			// Check if this is the start of a block
			if (IsMarker (lumpinfo + i, ustart))
			{
				insideBlock = true;

				// Create start marker if we haven't already
				if (!newlumps)
				{
					newlumps++;
					strncpy (newlumpinfos[0].name, ustart, 8);
					newlumpinfos[0].handle = -1;
					newlumpinfos[0].position =
						newlumpinfos[0].size = 0;
					newlumpinfos[0].namespc = ns_global;
				}
			}
			else
			{
				// Copy lumpinfo down this list
				lumpinfo[oldlumps++] = lumpinfo[i];
			}
		}
		else
		{
			// Check if this is the end of a block
			if (flatHack)
			{
				if (flatHack == i)
				{
					insideBlock = false;
					flatHack = 0;
				}
				else
				{
					if (lumpinfo[i].size != 4096)
					{
						lumpinfo[oldlumps++] = lumpinfo[i];
					}
					else
					{
						newlumpinfos[newlumps] = lumpinfo[i];
						newlumpinfos[newlumps++].namespc = space;
					}
				}
			}
			else if (i && lumpinfo[i].handle != lumpinfo[i-1].handle)
			{
				// Blocks cannot span multiple files
				insideBlock = false;
				lumpinfo[oldlumps++] = lumpinfo[i];
			}
			else if (IsMarker (lumpinfo + i, uend))
			{
				// It is the end of a block. We'll add the end marker once
				// we've processed everything.
				insideBlock = false;
			}
			else
			{
				newlumpinfos[newlumps] = lumpinfo[i];
				newlumpinfos[newlumps++].namespc = space;
			}
		}
	}

	// Now copy the merged lumps to the end of the old list
	// and create the end marker entry.

	if (newlumps)
	{
		if (oldlumps + newlumps > numlumps)
			lumpinfo = (lumpinfo_t *)Realloc (lumpinfo, oldlumps + newlumps);

		memcpy (lumpinfo + oldlumps, newlumpinfos, sizeof(lumpinfo_t) * newlumps);

		numlumps = oldlumps + newlumps;
		
		strncpy (lumpinfo[numlumps].name, uend, 8);
		lumpinfo[numlumps].handle = -1;
		lumpinfo[numlumps].position =
			lumpinfo[numlumps].size = 0;
		lumpinfo[numlumps].namespc = ns_global;
		numlumps++;
	}

	delete[] newlumpinfos;
}

//==========================================================================
//
// W_Profile
//
//==========================================================================

// [RH] Unused
void W_Profile (const char *fname)
{
#if 0
	int			i;
	memblock_t*	block;
	void*		ptr;
	char		ch;
	FILE*		f;
	int			j;
	char		name[9];
	
	f = fopen (fname,"wt");
	name[8] = 0;

	for (i=0 ; i<numlumps ; i++)
	{
		ptr = lumpcache ? lumpcache[i] : NULL;
		if (!ptr)
		{
			ch = ' ';
		}
		else
		{
			block = (memblock_t *) ( (byte *)ptr - sizeof(memblock_t));
			if (block->tag < PU_PURGELEVEL)
				ch = 'S';
			else
				ch = 'P';
		}
		memcpy (name,lumpinfo[i].name,8);

		for (j=0 ; j<8 ; j++)
			if (!name[j])
				break;

		for ( ; j<8 ; j++)
			name[j] = ' ';

		fprintf (f,"%s   %c  %u\n",name,ch,lumpinfo[i].namespc);
	}
	fclose (f);
#endif
}


//==========================================================================
//
// W_FindLump
//
// Find a named lump. Specifically allows duplicates for merging of e.g.
// SNDINFO lumps.
//
//==========================================================================

int W_FindLump (const char *name, int *lastlump)
{
	char name8[8];
	lumpinfo_t *lump_p;

	uppercopy (name8, name);

	lump_p = lumpinfo + *lastlump;
	while (lump_p < lumpinfo + numlumps)
	{
		if (*(__int64 *)&lump_p->name == *(__int64 *)&name8)
		{
			int lump = lump_p - lumpinfo;
			*lastlump = lump + 1;
			return lump;
		}
		lump_p++;
	}

	*lastlump = numlumps;
	return -1;
}

//==========================================================================
//
// W_CheckLumpName
//
//==========================================================================

BOOL W_CheckLumpName (int lump, const char *name)
{
	if (lump >= numlumps)
		return false;

	return !strnicmp (lumpinfo[lump].name, name, 8);
}

//==========================================================================
//
// W_GetLumpName
//
//==========================================================================

void W_GetLumpName (char *to, int lump)
{
	if (lump >= numlumps)
		*to = 0;
	else
		uppercopy (to, lumpinfo[lump].name);
}

//==========================================================================
//
// W_SkinHack
//
// Tests a wad file to see if it contains an S_SKIN marker. If it does,
// every lump in the wad is moved into a new namespace. Because skins are
// only supposed to replace player sprites, sounds, or faces, this should
// not be a problem. Yes, there are skins that replace more than that, but
// they are such a pain, and breaking them like this was done on purpose.
// This also renames any S_SKINxx lumps to just S_SKIN.
//==========================================================================

static void W_SkinHack (int baselump)
{
	bool skinned = false;
	int i;

	for (i = baselump; i < numlumps; i++)
	{
		if (lumpinfo[i].name[0] == 'S' &&
			lumpinfo[i].name[1] == '_' &&
			lumpinfo[i].name[2] == 'S' &&
			lumpinfo[i].name[3] == 'K' &&
			lumpinfo[i].name[4] == 'I' &&
			lumpinfo[i].name[5] == 'N')
		{ // Wad has at least one skin.
			lumpinfo[i].name[6] = lumpinfo[i].name[7] = 0;
			if (!skinned)
			{
				skinned = true;
				int j;

				for (j = baselump; j < numlumps; j++)
				{
					// Using the baselump as the namespace is safe, because
					// zdoom.wad guarantees the first possible baselump
					// passed to this function is a largish number.
					lumpinfo[j].namespc = baselump;
				}
			}
		}
	}
}