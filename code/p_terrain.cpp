// HEADER FILES ------------------------------------------------------------

#include <string.h>

#include "doomtype.h"
#include "cmdlib.h"
#include "p_terrain.h"
#include "gi.h"
#include "r_state.h"
#include "z_zone.h"
#include "w_wad.h"
#include "sc_man.h"
#include "s_sound.h"
#include "p_local.h"

// MACROS ------------------------------------------------------------------

#define SET_FIELD(type,val) *((type*)((byte *)fields + \
							parser[keyword].u.Offset)) = val;

// TYPES -------------------------------------------------------------------

enum EOuterKeywords
{
	OUT_SPLASH,
	OUT_TERRAIN,
	OUT_FLOOR,
	OUT_IFDOOM,
	OUT_IFHERETIC,
	OUT_IFHEXEN,
	OUT_ENDIF
};

enum ETerrainKeywords
{
	TR_CLOSE,
	TR_SPLASH,
	TR_DAMAGEAMOUNT,
	TR_DAMAGETYPE,
	TR_DAMAGETIMEMASK,
	TR_FOOTCLIP,
	TR_STEPVOLUME,
	TR_WALKINGSTEPTIME,
	TR_RUNNINGSTEPTIME,
	TR_LEFTSTEPSOUNDS,
	TR_RIGHTSTEPSOUNDS,
	TR_LIQUID,
	TR_LOWFRICTION
};

enum EDamageKeywords
{
	DAM_Lava,
	DAM_Ice,
	DAM_Slime
};

enum EGenericType
{
	GEN_End,
	GEN_Fixed,
	GEN_Sound,
	GEN_Byte,
	GEN_Class,
	GEN_Splash,
	GEN_Float,
	GEN_Time,
	GEN_Bool,
	GEN_Int,
	GEN_Custom,
};

struct FGenericParse
{
	EGenericType Type;
	union {
		size_t Offset;
		void (*Handler) (int type, void *fields);
	} u;
};

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

static void MakeDefaultTerrain ();
static void ParseOuter ();
static void ParseSplash ();
static void ParseTerrain ();
static void ParseFloor ();
static int FindSplash (const char *name);
static int FindTerrain (const char *name);
static void GenericParse (FGenericParse *parser, const char **keywords,
	void *fields, const char *type, const char *name);
static void ParseDamage (int keyword, void *fields);
static void ParseSounds (int keyword, void *fields);

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

byte *TerrainTypes;
TArray<FSplashDef> Splashes;
TArray<FTerrainDef> Terrains;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static const char *OuterKeywords[] =
{
	"splash",
	"terrain",
	"floor",
	"ifdoom",
	"ifheretic",
	"ifhexen",
	"endif",
	NULL
};

static const char *SplashKeywords[] =
{
	"}",
	"smallsound",
	"smallclip",
	"sound",
	"smallclass",
	"baseclass",
	"chunkclass",
	"chunkxvelshift",
	"chunkyvelshift",
	"chunkzvelshift",
	"chunkbasezvel",
	NULL
};

static const char *TerrainKeywords[] =
{
	"}",
	"splash",
	"damageamount",
	"damagetype",
	"damagetimemask",
	"footclip",
	"stepvolume",
	"walkingsteptime",
	"runningsteptime",
	"leftstepsounds",
	"rightstepsounds",
	"liquid",
	"lowfriction",
	NULL
};

static const char *DamageKeywords[] =
{
	"lava",
	"ice",
	"slime",
	NULL
};

static FGenericParse SplashParser[] =
{
	{ GEN_End, 0 },
	{ GEN_Sound,  {myoffsetof(FSplashDef, SmallSplashSound)} },
	{ GEN_Fixed,  {myoffsetof(FSplashDef, SmallSplashClip)} },
	{ GEN_Sound,  {myoffsetof(FSplashDef, NormalSplashSound)} },
	{ GEN_Class,  {myoffsetof(FSplashDef, SmallSplash)} },
	{ GEN_Class,  {myoffsetof(FSplashDef, SplashBase)} },
	{ GEN_Class,  {myoffsetof(FSplashDef, SplashChunk)} },
	{ GEN_Byte,   {myoffsetof(FSplashDef, ChunkXVelShift)} },
	{ GEN_Byte,   {myoffsetof(FSplashDef, ChunkYVelShift)} },
	{ GEN_Byte,   {myoffsetof(FSplashDef, ChunkZVelShift)} },
	{ GEN_Fixed,  {myoffsetof(FSplashDef, ChunkBaseZVel)} }
};

static FGenericParse TerrainParser[] =
{
	{ GEN_End, 0 },
	{ GEN_Splash, {myoffsetof(FTerrainDef, Splash)} },
	{ GEN_Int,    {myoffsetof(FTerrainDef, DamageAmount)} },
	{ GEN_Custom, {(size_t)ParseDamage} },
	{ GEN_Int,    {myoffsetof(FTerrainDef, DamageTimeMask)} },
	{ GEN_Fixed,  {myoffsetof(FTerrainDef, FootClip)} },
	{ GEN_Float,  {myoffsetof(FTerrainDef, StepVolume)} },
	{ GEN_Time,   {myoffsetof(FTerrainDef, WalkStepTics)} },
	{ GEN_Time,   {myoffsetof(FTerrainDef, RunStepTics)} },
	{ GEN_Custom, {(size_t)ParseSounds} },
	{ GEN_Custom, {(size_t)ParseSounds} },
	{ GEN_Bool,   {myoffsetof(FTerrainDef, IsLiquid)} },
	{ GEN_Bool,   {myoffsetof(FTerrainDef, ReducedFriction)} }
};

/*
struct
{
	char *name;
	int type;
	bool Heretic;
}
 TerrainTypeDefs[] =
{
	{ "FLTWAWA1", FLOOR_WATER, true },
	{ "FLTFLWW1", FLOOR_WATER, true },
	{ "FLTLAVA1", FLOOR_LAVA, true },
	{ "FLATHUH1", FLOOR_LAVA, true },
	{ "FLTSLUD1", FLOOR_SLUDGE, true },
	{ "X_005", FLOOR_WATER, false },
	{ "X_001", FLOOR_LAVA, false },
	{ "X_009", FLOOR_SLUDGE, false },
	{ "F_033", FLOOR_ICE, false },
	{ "END", -1 }
};
*/

// CODE --------------------------------------------------------------------

//==========================================================================
//
// P_InitTerrainTypes
//
//==========================================================================

void P_InitTerrainTypes ()
{
	int lastlump;
	int lump;
	int size;

	size = (numflats+1)*sizeof(byte);
	TerrainTypes = (byte *)Z_Malloc (size, PU_STATIC, 0);
	memset (TerrainTypes, 0, size);

	MakeDefaultTerrain ();

	lastlump = 0;
	while (-1 != (lump = W_FindLump ("TERRAIN", &lastlump)) )
	{
		SC_OpenLumpNum (lump, "TERRAIN");
		ParseOuter ();
		SC_Close ();
	}
	Splashes.ShrinkToFit ();
	Terrains.ShrinkToFit ();
}

//==========================================================================
//
// MakeDefaultTerrain
//
//==========================================================================

static void MakeDefaultTerrain ()
{
	FTerrainDef def;

	memset (&def, 0, sizeof(def));
	def.Name = copystring ("Solid");
	def.Splash = -1;
	Terrains.Push (def);
}

//==========================================================================
//
// ParseOuter
//
//==========================================================================

static void ParseOuter ()
{
	int bracedepth = 0;
	bool ifskip = false;

	while (SC_GetString ())
	{
		if (ifskip)
		{
			if (bracedepth > 0)
			{
				if (SC_Compare ("}"))
				{
					bracedepth--;
					continue;
				}
			}
			else if (SC_Compare ("endif"))
			{
				ifskip = false;
				continue;
			}
			if (SC_Compare ("{"))
			{
				bracedepth++;
			}
			else if (SC_Compare ("}"))
			{
				SC_ScriptError ("Too many left braces ('}')");
			}
		}
		else
		{
			switch (SC_MustMatchString (OuterKeywords))
			{
			case OUT_SPLASH:
				ParseSplash ();
				break;

			case OUT_TERRAIN:
				ParseTerrain ();
				break;

			case OUT_FLOOR:
				ParseFloor ();
				break;

			case OUT_IFDOOM:
				if (gameinfo.gametype != GAME_Doom)
				{
					ifskip = true;
				}
				break;

			case OUT_IFHERETIC:
				if (gameinfo.gametype != GAME_Heretic)
				{
					ifskip = true;
				}
				break;

			case OUT_IFHEXEN:
				if (gameinfo.gametype != GAME_Hexen)
				{
					ifskip = true;
				}
				break;

			case OUT_ENDIF:
				break;
			}
		}
	}
}

//==========================================================================
//
// ParseSplash
//
//==========================================================================

void ParseSplash ()
{
	int splashnum;
	FSplashDef *splashdef;
	bool isnew = false;;

	SC_MustGetString ();
	splashnum = FindSplash (sc_String);
	if (splashnum < 0)
	{
		FSplashDef def;
		def.Name = copystring (sc_String);
		splashnum = Splashes.Push (def);
		isnew = true;
	}
	splashdef = &Splashes[splashnum];

	SC_MustGetString ();
	if (!SC_Compare ("modify") || isnew)
	{ // Set defaults
		splashdef->SmallSplashSound =
			splashdef->NormalSplashSound = -1;
		splashdef->SmallSplash =
			splashdef->SplashBase =
			splashdef->SplashChunk = NULL;
		splashdef->ChunkXVelShift =
			splashdef->ChunkYVelShift =
			splashdef->ChunkZVelShift = 8;
		splashdef->ChunkBaseZVel = FRACUNIT;
		splashdef->SmallSplashClip = 12*FRACUNIT;
	}
	if (SC_Compare ("modify"))
	{
		SC_MustGetString ();
	}
	if (!SC_Compare ("{"))
	{
		SC_ScriptError ("Expected {");
	}
	else
	{
		GenericParse (SplashParser, SplashKeywords, splashdef, "splash",
			splashdef->Name);
	}
}

//==========================================================================
//
// ParseTerrain
//
//==========================================================================

void ParseTerrain ()
{
	int terrainnum;
	const char *name;

	SC_MustGetString ();
	terrainnum = FindTerrain (sc_String);
	if (terrainnum < 0)
	{
		FTerrainDef def;
		memset (&def, 0, sizeof(def));
		def.Splash = -1;
		def.Name = copystring (sc_String);
		terrainnum = Terrains.Push (def);
	}

	// Set defaults
	SC_MustGetString ();
	if (!SC_Compare ("modify"))
	{
		name = Terrains[terrainnum].Name;
		memset (&Terrains[terrainnum], 0, sizeof(FTerrainDef));
		Terrains[terrainnum].Splash = -1;
		Terrains[terrainnum].Name = name;
	}
	else
	{
		SC_MustGetString ();
	}

	if (SC_Compare ("{"))
	{
		GenericParse (TerrainParser, TerrainKeywords, &Terrains[terrainnum],
			"terrain", Terrains[terrainnum].Name);
	}
	else
	{
		SC_ScriptError ("Expected {");
	}
}

//==========================================================================
//
// ParseDamage
//
//==========================================================================

static void ParseDamage (int keyword, void *fields)
{
	FTerrainDef *def = (FTerrainDef *)fields;

	SC_MustGetString ();
	switch (SC_MustMatchString (DamageKeywords))
	{
	case DAM_Lava:
		def->DamageMOD = MOD_LAVA;
		def->DamageFlags = DMG_FIRE_DAMAGE;
		break;

	case DAM_Ice:
		def->DamageMOD = MOD_ICE;
		def->DamageFlags = DMG_ICE_DAMAGE;
		break;

	case DAM_Slime:
		def->DamageMOD = MOD_SLIME;
		def->DamageFlags = 0;
		break;
	}
}

//==========================================================================
//
// ParseSounds
//
//==========================================================================

static void ParseSounds (int keyword, void *fields)
{
	FTerrainDef *def = (FTerrainDef *)fields;
	bool notdone = true;
	bool warned = false;
	int *array;
	byte *count;

	if (keyword == TR_LEFTSTEPSOUNDS)
	{
		array = &def->LeftStepSounds[0];
		count = &def->NumLeftStepSounds;
	}
	else
	{
		array = &def->RightStepSounds[0];
		count = &def->NumRightStepSounds;
	}
	*count = 0;
	SC_MustGetStringName ("{");
	do
	{
		SC_MustGetString ();
		if (SC_Compare ("}"))
		{
			notdone = false;
		}
		else if (*count < 4)
		{
			int id = S_FindSound (sc_String);
			if (id == -1)
			{
				Printf (PRINT_HIGH, "Unknown sound %s in terrain %s\n",
					sc_String, def->Name);
			}
			else
			{
				array[*count] = id;
				*count++;
			}
		}
		else if (!warned)
		{
			warned = true;
			Printf (PRINT_HIGH, "Terrain %s has too many %s footstep sounds\n",
				def->Name, (keyword == TR_LEFTSTEPSOUNDS) ? "left" : "right");
		}
	} while (notdone);
}

//==========================================================================
//
// GenericParse
//
//==========================================================================

static void GenericParse (FGenericParse *parser, const char **keywords,
	void *fields, const char *type, const char *name)
{
	bool notdone = true;
	int keyword;
	int val;
	const TypeInfo *info;

	do
	{
		SC_MustGetString ();
		keyword = SC_MustMatchString (keywords);
		switch (parser[keyword].Type)
		{
		case GEN_End:
			notdone = false;
			break;

		case GEN_Fixed:
			SC_MustGetFloat ();
			SET_FIELD (fixed_t, (fixed_t)(FRACUNIT * sc_Float));
			break;

		case GEN_Sound:
			SC_MustGetString ();
			val = S_FindSound (sc_String);
			SET_FIELD (int, val);
			if (val == -1)
			{
				Printf (PRINT_HIGH, "Unknown sound %s in %s %s\n",
					sc_String, type, name);
			}
			break;

		case GEN_Byte:
			SC_MustGetNumber ();
			SET_FIELD (byte, sc_Number);
			break;

		case GEN_Class:
			SC_MustGetString ();
			info = TypeInfo::IFindType (sc_String);
			if (!info->IsDescendantOf (RUNTIME_CLASS(AActor)))
			{
				Printf (PRINT_HIGH, "%s is not an Actor (in %s %s)\n",
					sc_String, type, name);
				info = NULL;
			}
			else if (info == NULL)
			{
				Printf (PRINT_HIGH, "Unknown actor %s in %s %s\n",
					sc_String, type, name);
			}
			SET_FIELD (const TypeInfo *, info);
			break;

		case GEN_Splash:
			SC_MustGetString ();
			val = FindSplash (sc_String);
			SET_FIELD (int, val);
			if (val == -1)
			{
				Printf (PRINT_HIGH, "Splash %s is not defined yet (in %s %s)\n",
					sc_String, type, name);
			}
			break;

		case GEN_Float:
			SC_MustGetFloat ();
			SET_FIELD (float, sc_Float);
			break;

		case GEN_Time:
			SC_MustGetFloat ();
			SET_FIELD (int, (int)(sc_Float * TICRATE));
			break;

		case GEN_Bool:
			SET_FIELD (bool, true);
			break;

		case GEN_Int:
			SC_MustGetNumber ();
			SET_FIELD (int, sc_Number);
			break;

		case GEN_Custom:
			parser[keyword].u.Handler (keyword, fields);
			break;
		}
	} while (notdone);
}

//==========================================================================
//
// ParseFloor
//
//==========================================================================

static void ParseFloor ()
{
	int lump;
	int terrain;

	SC_MustGetString ();
	lump = W_CheckNumForName (sc_String, ns_flats);
	if (lump == -1)
	{
		Printf (PRINT_HIGH, "Unknown flat %s\n", sc_String);
		SC_MustGetString ();
		return;
	}
	SC_MustGetString ();
	terrain = FindTerrain (sc_String);
	if (terrain == -1)
	{
		Printf (PRINT_HIGH, "Unknown terrain %s\n", sc_String);
		terrain = 0;
	}
	TerrainTypes[lump - firstflat] = terrain;
}

//==========================================================================
//
// FindSplash
//
//==========================================================================

int FindSplash (const char *name)
{
	size_t i;

	for (i = 0; i < Splashes.Size (); i++)
	{
		if (stricmp (Splashes[i].Name, name) == 0)
		{
			return i;
		}
	}
	return -1;
}

//==========================================================================
//
// FindTerrain
//
//==========================================================================

int FindTerrain (const char *name)
{
	size_t i;

	for (i = 0; i < Terrains.Size (); i++)
	{
		if (stricmp (Terrains[i].Name, name) == 0)
		{
			return i;
		}
	}
	return -1;
}