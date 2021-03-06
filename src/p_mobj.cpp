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
//		Moving object handling. Spawn functions.
//
//-----------------------------------------------------------------------------

// HEADER FILES ------------------------------------------------------------

#include "templates.h"
#include "m_alloc.h"
#include "i_system.h"
#include "m_random.h"
#include "doomdef.h"
#include "p_local.h"
#include "p_lnspec.h"
#include "p_effect.h"
#include "p_terrain.h"
#include "st_stuff.h"
#include "hu_stuff.h"
#include "s_sound.h"
#include "doomstat.h"
#include "v_video.h"
#include "c_cvars.h"
#include "b_bot.h"	//Added by MC:
#include "stats.h"
#include "a_doomglobal.h"
#include "a_sharedglobal.h"
#include "gi.h"
#include "sbar.h"
#include "p_acs.h"
#include "cmdlib.h"
#include "decallib.h"
#include "ravenshared.h"
#include "a_action.h"
#include "a_keys.h"
#include "p_conversation.h"

// MACROS ------------------------------------------------------------------

#define WATER_SINK_FACTOR		3
#define WATER_SINK_SMALL_FACTOR	4
#define WATER_SINK_SPEED		(FRACUNIT/2)
#define WATER_JUMP_SPEED		(FRACUNIT*7/2)

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

void G_PlayerReborn (int player);

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

static void PlayerLandedOnThing (AActor *mo, AActor *onmobj);

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern cycle_t BotSupportCycles;
extern cycle_t BotWTG;
extern fixed_t attackrange;
extern int tmfloorpic;
extern sector_t *tmfloorsector;
EXTERN_CVAR (Bool, r_drawfuzz);

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static bool SpawningMapThing;
static FRandom pr_explodemissile ("ExplodeMissile");
static FRandom pr_bounce ("Bounce");
static FRandom pr_reflect ("Reflect");
static FRandom pr_nightmarerespawn ("NightmareRespawn");
static FRandom pr_botspawnmobj ("BotSpawnActor");
static FRandom pr_spawnmapthing ("SpawnMapThing");
static FRandom pr_spawnpuff ("SpawnPuff");
static FRandom pr_spawnblood ("SpawnBlood");
static FRandom pr_splatter ("BloodSplatter");
static FRandom pr_ripperblood ("RipperBlood");
static FRandom pr_chunk ("Chunk");
static FRandom pr_checkmissilespawn ("CheckMissileSpawn");
static FRandom pr_spawnmissile ("SpawnMissile");
 FRandom pr_slam ("SkullSlam");
static FRandom pr_multiclasschoice ("MultiClassChoice");

// PUBLIC DATA DEFINITIONS -------------------------------------------------

FRandom pr_spawnmobj ("SpawnActor");

CUSTOM_CVAR (Float, sv_gravity, 800.f, CVAR_SERVERINFO|CVAR_NOSAVE)
{
	level.gravity = self;
}

CVAR (Bool, cl_missiledecals, true, CVAR_ARCHIVE)
CVAR (Bool, addrocketexplosion, false, CVAR_ARCHIVE)

fixed_t FloatBobOffsets[64] =
{
	0, 51389, 102283, 152192,
	200636, 247147, 291278, 332604,
	370727, 405280, 435929, 462380,
	484378, 501712, 514213, 521763,
	524287, 521763, 514213, 501712,
	484378, 462380, 435929, 405280,
	370727, 332604, 291278, 247147,
	200636, 152192, 102283, 51389,
	-1, -51390, -102284, -152193,
	-200637, -247148, -291279, -332605,
	-370728, -405281, -435930, -462381,
	-484380, -501713, -514215, -521764,
	-524288, -521764, -514214, -501713,
	-484379, -462381, -435930, -405280,
	-370728, -332605, -291279, -247148,
	-200637, -152193, -102284, -51389
};

fixed_t FloatBobDiffs[64] =
{
	51389, 51389, 50894, 49909, 48444,
	46511, 44131, 41326, 38123,
	34553, 30649, 26451, 21998,
	17334, 12501, 7550, 2524,
	-2524, -7550, -12501, -17334,
	-21998, -26451, -30649, -34553,
	-38123, -41326, -44131, -46511,
	-48444, -49909, -50894, -51390,
	-51389, -50894, -49909, -48444,
	-46511, -44131, -41326, -38123,
	-34553, -30649, -26451, -21999,
	-17333, -12502, -7549, -2524,
	2524, 7550, 12501, 17334,
	21998, 26451, 30650, 34552,
	38123, 41326, 44131, 46511,
	48444, 49909, 50895
};

CVAR (Int, cl_pufftype, 0, CVAR_ARCHIVE);
CVAR (Int, cl_bloodtype, 0, CVAR_ARCHIVE);

AActor *MissileActor;
AActor *PuffSpawned;

// CODE --------------------------------------------------------------------

IMPLEMENT_POINTY_CLASS (AActor)
 DECLARE_POINTER (target)
 DECLARE_POINTER (lastenemy)
 DECLARE_POINTER (tracer)
 DECLARE_POINTER (goal)
 DECLARE_POINTER (LastLook)	// This is actually a union
 DECLARE_POINTER (Inventory)
 DECLARE_POINTER (LastHeard)
END_POINTERS

ACTOR_STATE_NAMES(AActor) =
{
	{ &AActor::SpawnState,		"Spawn" },
	{ &AActor::SeeState,		"See" },
	{ &AActor::PainState,		"Pain" },
	{ &AActor::MeleeState,		"Melee" },
	{ &AActor::MissileState,	"Missile" },
	{ &AActor::CrashState,		"Crash" },
	{ &AActor::DeathState,		"Death" },
	{ &AActor::XDeathState,		"XDeath" },
	{ &AActor::BDeathState,		"Burn" },
	{ &AActor::IDeathState,		"Ice" },
	{ &AActor::EDeathState,		"Disintegrate" },
	{ &AActor::RaiseState,		"Raise" },
	{ &AActor::WoundState,		"Wound" },
	{ 0, NAME_None }
};

AActor::~AActor ()
{
	// Please avoid calling the destructor directly (or through delete)!
	// Use Destroy() instead.
}

void AActor::Serialize (FArchive &arc)
{
	Super::Serialize (arc);

	if (arc.IsStoring ())
	{
		arc.WriteSprite (sprite);
	}
	else
	{
		sprite = arc.ReadSprite ();
	}

	arc << x
		<< y
		<< z
		<< angle
		<< frame
		<< xscale
		<< yscale
		<< RenderStyle
		<< renderflags;
	if (arc.IsStoring ())
	{
		BYTE ff;
		if (picnum == 0xFFFF)
		{
			ff = 0xFF;
			arc << ff;
		}
		else
		{
			ff = 0;
			arc << ff;
			TexMan.WriteTexture (arc, picnum);
		}
		TexMan.WriteTexture (arc, floorpic);
	}
	else
	{
		BYTE ff;
		arc << ff;
		if (ff == 0xFF)
		{
			picnum = 0xFFFF;
		}
		else
		{
			picnum = TexMan.ReadTexture (arc);
		}
		if (SaveVersion >= 227)
		{
			floorpic = TexMan.ReadTexture (arc);
		}
	}
	arc << TIDtoHate;
	if (TIDtoHate == 0)
	{
		arc << LastLook.PlayerNumber;
	}
	else
	{
		arc << LastLook.Actor;
	}
	arc << effects
		<< alpha
		<< alphacolor
		<< pitch
		<< roll
		<< Sector
		<< floorz
		<< ceilingz
		<< dropoffz
		<< floorsector;
	if (SaveVersion < 227)
	{
		arc << floorpic;
	}
	arc << radius
		<< height
		<< momx
		<< momy
		<< momz
		<< tics
		<< state
		<< damage
		<< flags
		<< flags2
		<< flags3
		<< flags4
		<< special1
		<< special2
		<< health
		<< movedir
		<< visdir
		<< movecount
		<< target
		<< lastenemy
		<< LastHeard
		<< reactiontime
		<< threshold
		<< player
		<< SpawnPoint[0] << SpawnPoint[1] << SpawnPoint[2]
		<< SpawnAngle
		<< tracer
		<< floorclip
		<< tid
		<< special
		<< args[0] << args[1] << args[2] << args[3] << args[4]
		<< goal
		<< waterlevel
		<< MinMissileChance
		<< SpawnFlags
		<< Inventory
		<< InventoryID
		<< id
		<< FloatBobPhase
		<< Translation
		<< AR_SOUNDW(SeeSound)
		<< AR_SOUNDW(AttackSound)
		<< AR_SOUNDW(PainSound)
		<< AR_SOUNDW(DeathSound)
		<< AR_SOUNDW(ActiveSound)
		<< AR_SOUNDW(UseSound)
		<< Speed
		<< Mass
		<< PainChance
		<< SpawnState
		<< SeeState
		<< PainState
		<< MeleeState
		<< MissileState
		<< CrashState
		<< DeathState
		<< XDeathState
		<< BDeathState
		<< IDeathState
		<< EDeathState
		<< RaiseState
		<< WoundState;
	if (SaveVersion >= 224)
	{
		arc << DamageType;
	}
	if (arc.IsStoring ())
	{
		int convnum = 0;
		unsigned int i;

		if (Conversation != NULL)
		{
			for (i = 0; i < StrifeDialogues.Size(); ++i)
			{
				if (StrifeDialogues[i] == GetDefault()->Conversation)
				{
					break;
				}
			}
			for (; i + convnum < StrifeDialogues.Size(); ++convnum)
			{
				if (StrifeDialogues[i + convnum] == Conversation)
				{
					break;
				}
			}
			if (i + convnum < StrifeDialogues.Size())
			{
				convnum++;
			}
			else
			{
				convnum = 0;
			}
		}
		arc.WriteCount (convnum);
	}
	else
	{
		int convnum;
		unsigned int i;

		convnum = arc.ReadCount();
		if (convnum == 0 || GetDefault()->Conversation == NULL)
		{
			Conversation = NULL;
		}
		else
		{
			for (i = 0; i < StrifeDialogues.Size(); ++i)
			{
				if (StrifeDialogues[i] == GetDefault()->Conversation)
				{
					break;
				}
			}
			if (i + convnum < StrifeDialogues.Size())
			{
				Conversation = StrifeDialogues[i + convnum - 1];
			}
			else
			{
				Conversation = GetDefault()->Conversation;
			}
		}
	}

	if (arc.IsLoading ())
	{
		touching_sectorlist = NULL;
		LinkToWorld (Sector);
		AddToHash ();
		SetShade (alphacolor);
		if (player)
		{
			if (playeringame[player - players])
			{ // Give player back the skin
				player->skin = &skins[player->userinfo.skin];
			}
			if (Speed == 0)
			{
				Speed = GetDefault()->Speed;
			}
		}
		PrevX = x;
		PrevY = y;
		PrevZ = z;
	}
}

void MapThing::Serialize (FArchive &arc)
{
	arc << thingid << x << y << z << angle << type << flags << special
		<< args[0] << args[1] << args[2] << args[3] << args[4];
}

AActor::AActor () throw()
{
	memset (&x, 0, (byte *)&this[1] - (byte *)&x);
}

AActor::AActor (const AActor &other) throw()
{
	memcpy (&x, &other.x, (byte *)&this[1] - (byte *)&x);
}

AActor &AActor::operator= (const AActor &other)
{
	memcpy (&x, &other.x, (byte *)&this[1] - (byte *)&x);
	return *this;
}

//==========================================================================
//
// AActor::SetState
//
// Returns true if the mobj is still present.
//
//==========================================================================

bool AActor::SetState (FState *newstate)
{
	if (debugfile && player && (player->cheats & CF_PREDICTING))
		fprintf (debugfile, "for pl %d: SetState while predicting!\n", player-players);
	do
	{
		if (newstate == NULL)
		{
			state = NULL;
			Destroy ();
			return false;
		}
		int prevsprite, newsprite;

		if (state != NULL)
		{
			prevsprite = state->sprite.index;
		}
		else
		{
			prevsprite = -1;
		}
		state = newstate;
		tics = newstate->GetTics();
		renderflags = (renderflags & ~RF_FULLBRIGHT) | newstate->GetFullbright();
		newsprite = newstate->sprite.index;
		if (newsprite != 1)
		{
			// Sprite 1 is ----, which means "do not change the sprite"
			frame = newstate->GetFrame();

			if (!(flags4 & MF4_NOSKIN) && newsprite == SpawnState->sprite.index)
			{ // [RH] If the new sprite is the same as the original sprite, and
			// this actor is attached to a player, use the player's skin's
			// sprite. If a player is not attached, do not change the sprite
			// unless it is different from the previous state's sprite; a
			// player may have been attached, died, and respawned elsewhere,
			// and we do not want to lose the skin on the body. If it wasn't
			// for Dehacked, I would move sprite changing out of the states
			// altogether, since actors rarely change their sprites after
			// spawning.
				if (player != NULL)
				{
					sprite = skins[player->userinfo.skin].sprite;
				}
				else if (newsprite != prevsprite)
				{
					sprite = newsprite;
				}
			}
			else
			{
				sprite = newsprite;
			}
		}
		if (newstate->GetAction())
		{
			newstate->GetAction() (this);
		}
		newstate = newstate->GetNextState();
	} while (tics == 0);

	return true;
}

//----------------------------------------------------------------------------
//
// FUNC AActor::SetStateNF
//
// Same as SetState, but does not call the state function.
//
//----------------------------------------------------------------------------

bool AActor::SetStateNF (FState *newstate)
{
	do
	{
		if (newstate == NULL)
		{
			state = NULL;
			Destroy ();
			return false;
		}
		int prevsprite, newsprite;

		if (state != NULL)
		{
			prevsprite = state->sprite.index;
		}
		else
		{
			prevsprite = -1;
		}
		state = newstate;
		tics = newstate->GetTics();
		renderflags = (renderflags & ~RF_FULLBRIGHT) | newstate->GetFullbright();
		newsprite = newstate->sprite.index;
		if (newsprite != 1)
		{
			// Sprite 1 is ----, which means "do not change the sprite"

			frame = newstate->GetFrame();
			if (!(flags4 & MF4_NOSKIN) && newsprite == SpawnState->sprite.index)
			{
				if (player != NULL && gameinfo.gametype != GAME_Hexen)
				{
					sprite = skins[player->userinfo.skin].sprite;
				}
				else if (newsprite != prevsprite)
				{
					sprite = newsprite;
				}
			}
			else
			{
				sprite = newsprite;
			}
		}
		newstate = newstate->GetNextState();
	} while (tics == 0);

	return true;
}

//============================================================================
//
// AActor :: AddInventory
//
//============================================================================

void AActor::AddInventory (AInventory *item)
{
	// Check if it's already attached to an actor
	if (item->Owner != NULL)
	{
		// Is it attached to us?
		if (item->Owner == this)
			return;

		// No, then remove it from the other actor first
		item->Owner->RemoveInventory (item);
	}

	item->Owner = this;
	item->Inventory = Inventory;
	Inventory = item;

	// Each item receives an unique ID when added to an actor's inventory.
	// This is used by the DEM_INVUSE command to identify the item. Simply
	// using the item's position in the list won't work, because ticcmds get
	// run sometime in the future, so by the time it runs, the inventory
	// might not be in the same state as it was when DEM_INVUSE was sent.
	Inventory->InventoryID = InventoryID++;
}

//============================================================================
//
// AActor :: RemoveInventory
//
//============================================================================

void AActor::RemoveInventory (AInventory *item)
{
	AInventory *inv, **invp;

	invp = &item->Owner->Inventory;
	for (inv = *invp; inv != NULL; invp = &inv->Inventory, inv = *invp)
	{
		if (inv == item)
		{
			*invp = item->Inventory;
			item->DetachFromOwner ();
			item->Owner = NULL;
			break;
		}
	}
}

//============================================================================
//
// AActor :: FirstInv
//
// Returns the first item in this actor's inventory that has IF_INVBAR set.
//
//============================================================================

AInventory *AActor::FirstInv () const
{
	if (Inventory == NULL)
	{
		return NULL;
	}
	if (Inventory->ItemFlags & IF_INVBAR)
	{
		return Inventory;
	}
	return Inventory->NextInv ();
}

//============================================================================
//
// AActor :: UseInventory
//
// Attempts to use an item. If the use succeeds, one copy of the item is
// removed from the inventory. If all copies are removed, then the item is
// destroyed.
//
//============================================================================

bool AActor::UseInventory (AInventory *item)
{
	// No using items if you're dead.
	if (health <= 0)
	{
		return false;
	}
	if (!item->Use (false))
	{
		return false;
	}
	if (--item->Amount <= 0)
	{
		item->Destroy ();
	}
	return true;
}

//===========================================================================
//
// AActor :: DropInventory
//
// Removes a single copy of an item and throws it out in front of the actor.
//
//===========================================================================

AInventory *AActor::DropInventory (AInventory *item)
{
	fixed_t dropdist;
	angle_t an;
	AInventory *drop = item->CreateTossable ();

	if (drop == NULL)
	{
		return NULL;
	}
	an = angle >> ANGLETOFINESHIFT;
	/* 92682 = sqrt(2) * FRACUNIT */
	dropdist = FixedMul (92682, radius + 8*FRACUNIT + item->radius);
	drop->x = x;
	drop->y = y;
	drop->z = z + 10*FRACUNIT;
	P_TryMove (drop, x, y, true);
	drop->angle = angle;
	drop->momx = momx + 5 * finecosine[an];
	drop->momy = momy + 5 * finesine[an];
	drop->momz = momz + FRACUNIT;
	drop->flags &= ~MF_NOGRAVITY;	// Don't float
	return drop;
}

//============================================================================
//
// AActor :: FindInventory
//
//============================================================================

AInventory *AActor::FindInventory (const TypeInfo *type) const
{
	AInventory *item;

	for (item = Inventory; item != NULL; item = item->Inventory)
	{
		if (item->GetClass() == type)
		{
			break;
		}
	}
	return item;
}

//============================================================================
//
// AActor :: GiveInventoryType
//
//============================================================================

AInventory *AActor::GiveInventoryType (const TypeInfo *type)
{
	AInventory *item;

	item = static_cast<AInventory *>(Spawn (type, 0,0,0));
	if (!item->TryPickup (this))
	{
		item->Destroy ();
		return NULL;
	}
	return item;
}

//============================================================================
//
// AActor :: GiveAmmo
//
// Returns true if the ammo was added, false if not.
//
//============================================================================

bool AActor::GiveAmmo (const TypeInfo *type, int amount)
{
	AInventory *item = static_cast<AInventory *>(Spawn (type, 0, 0, 0));
	item->Amount = amount;
	item->flags |= MF_DROPPED;
	if (!item->TryPickup (this))
	{
		item->Destroy ();
		return false;
	}
	return true;
}

//============================================================================
//
// AActor :: CopyFriendliness
//
// Makes this actor hate (or like) the same things another actor does.
//
//============================================================================

void AActor::CopyFriendliness (const AActor *other, bool changeTarget)
{
	TIDtoHate = other->TIDtoHate;
	LastLook = other->LastLook;
	flags  = (flags & ~MF_FRIENDLY) | (other->flags & MF_FRIENDLY);
	flags3 = (flags3 & ~(MF3_NOSIGHTCHECK | MF3_HUNTPLAYERS)) | (other->flags3 & (MF3_NOSIGHTCHECK | MF3_HUNTPLAYERS));
	flags4 = (flags4 & ~MF4_NOHATEPLAYERS) | (other->flags4 & MF4_NOHATEPLAYERS);
	FriendPlayer = other->FriendPlayer;
	if (changeTarget)
	{
		target = other->target;
	}
}

//============================================================================
//
// AActor :: ObtainInventory
//
// Removes the items from the other actor and puts them in this actor's
// inventory. The actor receiving the inventory must not have any items.
//
//============================================================================

void AActor::ObtainInventory (AActor *other)
{
	Inventory = other->Inventory;
	InventoryID = other->InventoryID;
	other->Inventory = NULL;
	other->InventoryID = 0;

	AInventory *item = Inventory;
	while (item != NULL)
	{
		item->Owner = this;
		item = item->Inventory;
	}
}

//============================================================================
//
// AActor :: CheckLocalView
//
// Returns true if this actor is local for the player. Here, local means the
// player is either looking out this actor's eyes, or this actor is the player
// and the player is looking out the eyes of something non-"sentient."
//
//============================================================================

bool AActor::CheckLocalView (int playernum) const
{
	if (players[playernum].camera == this)
	{
		return true;
	}
	if (players[playernum].mo != this || players[playernum].camera == NULL)
	{
		return false;
	}
	if (players[playernum].camera->player == NULL &&
		!(players[playernum].camera->flags3 & MF3_ISMONSTER))
	{
		return true;
	}
	return false;
}

//============================================================================
//
// AActor :: ConversationAnimation
//
// Plays a conversation-related animation:
//	 0 = greeting
//   1 = "yes"
//   2 = "no"
//
//============================================================================

void AActor::ConversationAnimation (int animnum)
{
}

//============================================================================
//
// AActor :: Touch
//
// Something just touched this actor. Normally used only for inventory items,
// but some Strife monsters also use it.
//
//============================================================================

void AActor::Touch (AActor *toucher)
{
}

//----------------------------------------------------------------------------
//
// PROC P_ExplodeMissile
//
//----------------------------------------------------------------------------

void P_ExplodeMissile (AActor *mo, line_t *line)
{
	if (mo->flags3 & MF3_EXPLOCOUNT)
	{
		if (++mo->special2 < mo->special1)
		{
			return;
		}
	}
	mo->momx = mo->momy = mo->momz = 0;
	mo->effects = 0;		// [RH]
	mo->SetState (mo->DeathState);
	if (mo->ObjectFlags & OF_MassDestruction)
	{
		return;
	}

	if (line != NULL && line->special == Line_Horizon)
	{
		// [RH] Don't explode missiles on horizon lines.
		mo->Destroy ();
		return;
	}

	if (line != NULL && cl_missiledecals)
	{
		int side = P_PointOnLineSide (mo->x, mo->y, line);
		if (line->sidenum[side] == NO_INDEX)
			side ^= 1;
		if (line->sidenum[side] != NO_INDEX)
		{
			FDecalBase *base = mo->DecalGenerator;
			if (base != NULL)
			{
				// Find the nearest point on the line, and stick a decal there
				fixed_t x, y, z;
				SQWORD num, den;

				den = (SQWORD)line->dx*line->dx + (SQWORD)line->dy*line->dy;
				if (den != 0)
				{
					SDWORD frac;

					num = (SQWORD)(mo->x-line->v1->x)*line->dx+(SQWORD)(mo->y-line->v1->y)*line->dy;
					if (num <= 0)
					{
						frac = 0;
					}
					else if (num >= den)
					{
						frac = 1<<30;
					}
					else
					{
						frac = (SDWORD)(num / (den>>30));
					}

					x = line->v1->x + MulScale30 (line->dx, frac);
					y = line->v1->y + MulScale30 (line->dy, frac);
					z = mo->z;

					AImpactDecal::StaticCreate (base->GetDecal (),
						x, y, z, sides + line->sidenum[side]);
				}
			}
		}
	}

	if (mo->DeathState != NULL)
	{
		// [RH] Change render style of exploding rockets
		if (mo->IsKindOf (RUNTIME_CLASS(ARocket)))
		{
			if (deh.ExplosionStyle == 255)
			{
				if (addrocketexplosion)
				{
					mo->RenderStyle = STYLE_Add;
					mo->alpha = FRACUNIT;
				}
				else
				{
					mo->RenderStyle = STYLE_Translucent;
					mo->alpha = FRACUNIT*2/3;
				}
			}
			else
			{
				mo->RenderStyle = deh.ExplosionStyle;
				mo->alpha = deh.ExplosionAlpha;
			}
		}

		if (gameinfo.gametype == GAME_Doom)
		{
			mo->tics -= (pr_explodemissile() & 3) * TICRATE / 35;
			if (mo->tics < 1)
				mo->tics = 1;
		}

		mo->flags &= ~MF_MISSILE;

		if (mo->DeathSound)
		{
			S_SoundID (mo, CHAN_VOICE, mo->DeathSound, 1,
				(mo->flags3 & MF3_FULLVOLDEATH) ? ATTN_NONE : ATTN_NORM);
		}
	}
}

//----------------------------------------------------------------------------
//
// PROC P_FloorBounceMissile
//
// Returns true if the missile was destroyed
//----------------------------------------------------------------------------

bool AActor::FloorBounceMissile (secplane_t &plane)
{
	if (z <= floorz && P_HitFloor (this) && !(flags3 & MF3_CANBOUNCEWATER))
	{ // Landed in some sort of liquid
		Destroy ();
		return true;
	}

	fixed_t dot = TMulScale16 (momx, plane.a, momy, plane.b, momz, plane.c);

	if ((flags2 & MF2_BOUNCETYPE) == MF2_HERETICBOUNCE)
	{
		momx -= MulScale15 (plane.a, dot);
		momy -= MulScale15 (plane.b, dot);
		momz -= MulScale15 (plane.c, dot);
		angle = R_PointToAngle2 (0, 0, momx, momy);
		flags |= MF_INBOUNCE;
		SetState (DeathState);
		flags &= ~MF_INBOUNCE;
		return false;
	}

	// The reflected velocity keeps only about 70% of its original speed
	momx = MulScale30 (momx - MulScale15 (plane.a, dot), 751619277);
	momy = MulScale30 (momy - MulScale15 (plane.b, dot), 751619277);
	momz = MulScale30 (momz - MulScale15 (plane.c, dot), 751619277);
	angle = R_PointToAngle2 (0, 0, momx, momy);

	if (SeeSound)
	{
		S_SoundID (this, CHAN_VOICE, SeeSound, 1, ATTN_IDLE);
	}

	if ((flags2 & MF2_BOUNCETYPE) == MF2_DOOMBOUNCE)
	{
		if (!(flags & MF_NOGRAVITY) && (momz < 3*FRACUNIT))
		{
			flags2 &= ~MF2_BOUNCETYPE;
		}
	}
	return false;
}

//----------------------------------------------------------------------------
//
// PROC P_ThrustMobj
//
//----------------------------------------------------------------------------

void P_ThrustMobj (AActor *mo, angle_t angle, fixed_t move)
{
	angle >>= ANGLETOFINESHIFT;
	mo->momx += FixedMul (move, finecosine[angle]);
	mo->momy += FixedMul (move, finesine[angle]);
}

//----------------------------------------------------------------------------
//
// FUNC P_FaceMobj
//
// Returns 1 if 'source' needs to turn clockwise, or 0 if 'source' needs
// to turn counter clockwise.  'delta' is set to the amount 'source'
// needs to turn.
//
//----------------------------------------------------------------------------

int P_FaceMobj (AActor *source, AActor *target, angle_t *delta)
{
	angle_t diff;
	angle_t angle1;
	angle_t angle2;

	angle1 = source->angle;
	angle2 = R_PointToAngle2 (source->x, source->y, target->x, target->y);
	if (angle2 > angle1)
	{
		diff = angle2 - angle1;
		if (diff > ANGLE_180)
		{
			*delta = ANGLE_MAX - diff;
			return 0;
		}
		else
		{
			*delta = diff;
			return 1;
		}
	}
	else
	{
		diff = angle1 - angle2;
		if (diff > ANGLE_180)
		{
			*delta = ANGLE_MAX - diff;
			return 1;
		}
		else
		{
			*delta = diff;
			return 0;
		}
	}
}

//----------------------------------------------------------------------------
//
// FUNC P_SeekerMissile
//
// The missile's tracer field must be the target.  Returns true if
// target was tracked, false if not.
//
//----------------------------------------------------------------------------

bool P_SeekerMissile (AActor *actor, angle_t thresh, angle_t turnMax)
{
	int dir;
	int dist;
	angle_t delta;
	angle_t angle;
	AActor *target;

	target = actor->tracer;
	if (target == NULL || actor->Speed == 0)
	{
		return false;
	}
	if (!(target->flags & MF_SHOOTABLE))
	{ // Target died
		actor->tracer = NULL;
		return false;
	}
	dir = P_FaceMobj (actor, target, &delta);
	if (delta > thresh)
	{
		delta >>= 1;
		if (delta > turnMax)
		{
			delta = turnMax;
		}
	}
	if (dir)
	{ // Turn clockwise
		actor->angle += delta;
	}
	else
	{ // Turn counter clockwise
		actor->angle -= delta;
	}
	angle = actor->angle>>ANGLETOFINESHIFT;
	actor->momx = FixedMul (actor->Speed, finecosine[angle]);
	actor->momy = FixedMul (actor->Speed, finesine[angle]);
	if (actor->z + actor->height < target->z ||
		target->z + target->height < actor->z)
	{ // Need to seek vertically
		dist = P_AproxDistance (target->x - actor->x, target->y - actor->y);
		dist = dist / actor->Speed;
		if (dist < 1)
		{
			dist = 1;
		}
		actor->momz = ((target->z+target->height/2) - (actor->z+actor->height/2)) / dist;
	}
	return true;
}

//
// P_XYMovement  
//
#define STOPSPEED			0x1000
#define FRICTION			0xe800
#define CARRYSTOPSPEED		(STOPSPEED*32/3)

void P_XYMovement (AActor *mo, fixed_t scrollx, fixed_t scrolly) 
{
	bool bForceSlide = scrollx || scrolly;
	angle_t angle;
	fixed_t ptryx, ptryy;
	player_t *player;
	fixed_t xmove, ymove;
	bool walkplane;
	static const int windTab[3] = {2048*5, 2048*10, 2048*25};
	int steps, step, totalsteps;
	fixed_t startx, starty;

	fixed_t maxmove = (mo->waterlevel < 2) || (mo->flags & MF_MISSILE) ? MAXMOVE : MAXMOVE/4;

	if (mo->flags2 & MF2_WINDTHRUST && mo->waterlevel < 2 && !(mo->flags & MF_NOCLIP))
	{
		int special = mo->Sector->special;
		switch (special)
		{
			case 40: case 41: case 42: // Wind_East
				P_ThrustMobj (mo, 0, windTab[special-40]);
				break;
			case 43: case 44: case 45: // Wind_North
				P_ThrustMobj (mo, ANG90, windTab[special-43]);
				break;
			case 46: case 47: case 48: // Wind_South
				P_ThrustMobj (mo, ANG270, windTab[special-46]);
				break;
			case 49: case 50: case 51: // Wind_West
				P_ThrustMobj (mo, ANG180, windTab[special-49]);
				break;
		}
	}

	// [RH] No need to clamp these now. However, wall running needs it so
	// that large thrusts can't propel an actor through a wall, because wall
	// running depends on the player's original movement continuing even after
	// it gets blocked.
	if (mo->player != NULL && (compatflags & COMPATF_WALLRUN) || (mo->waterlevel >= 2))
	{
		xmove = mo->momx = clamp (mo->momx, -maxmove, maxmove);
		ymove = mo->momy = clamp (mo->momy, -maxmove, maxmove);
	}
	else
	{
		xmove = mo->momx;
		ymove = mo->momy;
	}
	// [RH] Carrying sectors didn't work with low speeds in BOOM. This is
	// because BOOM relied on the speed being fast enough to accumulate
	// despite friction. If the speed is too low, then its movement will get
	// cancelled, and it won't accumulate to the desired speed.
	if (scrollx > CARRYSTOPSPEED)
	{
		scrollx = FixedMul (scrollx, CARRYFACTOR);
		mo->momx += scrollx;
	}
	if (scrolly > CARRYSTOPSPEED)
	{
		scrolly = FixedMul (scrolly, CARRYFACTOR);
		mo->momy += scrolly;
	}
	xmove += scrollx;
	ymove += scrolly;

	if ((xmove | ymove) == 0)
	{
		if (mo->flags & MF_SKULLFLY)
		{
			// the skull slammed into something
			mo->flags &= ~MF_SKULLFLY;
			mo->momx = mo->momy = mo->momz = 0;

			mo->SetState (mo->SeeState != NULL ? mo->SeeState : mo->SpawnState);
		}
		return;
	}

	player = mo->player;

	// [RH] Adjust player movement on sloped floors
	fixed_t startxmove = xmove;
	fixed_t startymove = ymove;
	walkplane = P_CheckSlopeWalk (mo, xmove, ymove);

	// [RH] Take smaller steps when moving faster than the object's size permits.
	// Moving as fast as the object's "diameter" is bad because it could skip
	// some lines because the actor could land such that it is just touching the
	// line. For Doom to detect that the line is there, it needs to actually cut
	// through the actor.

	{
		maxmove = mo->radius - FRACUNIT;

		if (maxmove <= 0)
		{ // gibs can have radius 0, so don't divide by zero below!
			maxmove = MAXMOVE;
		}

		const fixed_t xspeed = abs (xmove);
		const fixed_t yspeed = abs (ymove);

		steps = 1;

		if (xspeed > yspeed)
		{
			if (xspeed > maxmove)
			{
				steps = 1 + xspeed / maxmove;
			}
		}
		else
		{
			if (yspeed > maxmove)
			{
				steps = 1 + yspeed / maxmove;
			}
		}
	}

	// P_SlideMove needs to know the step size before P_CheckSlopeWalk
	// because it also calls P_CheckSlopeWalk on its clipped steps.
	fixed_t onestepx = startxmove / steps;
	fixed_t onestepy = startymove / steps;

	startx = mo->x;
	starty = mo->y;
	step = 1;
	totalsteps = steps;

	// [RH] Instead of doing ripping damage each step, do it each tic.
	// This makes it compatible with Heretic and Hexen, which only did
	// one step for their missiles with ripping damage (excluding those
	// that don't use P_XYMovement). It's also more intuitive since it
	// makes the damage done dependant on the amount of time the projectile
	// spends inside a target rather than on the projectile's size. The
	// last actor ripped through is recorded so that if the projectile
	// passes through more than one actor this tic, each one takes damage
	// and not just the first one.

	if (mo->flags2 & MF2_RIP)
	{
		DoRipping = true;
		LastRipped = NULL;
	}

	do
	{
		ptryx = startx + Scale (xmove, step, steps);
		ptryy = starty + Scale (ymove, step, steps);

/*		if (mo->player)
		Printf ("%d,%d/%d: %d %d %d %d %d %d %d\n", level.time, step, steps, startxmove, Scale(xmove,step,steps), startymove, Scale(ymove,step,steps), mo->x, mo->y, mo->z);
*/
		// [RH] If walking on a slope, stay on the slope
		// killough 3/15/98: Allow objects to drop off
		if (!P_TryMove (mo, ptryx, ptryy, true, walkplane))
		{
			// blocked move

			if (mo->flags2 & MF2_SLIDE || bForceSlide)
			{
				// try to slide along it
				if (BlockingMobj == NULL)
				{ // slide against wall
					if (BlockingLine != NULL &&
						mo->player && mo->waterlevel && mo->waterlevel < 3 &&
						(mo->player->cmd.ucmd.forwardmove | mo->player->cmd.ucmd.sidemove) &&
						BlockingLine->sidenum[1] != NO_INDEX)
					{
						mo->momz = WATER_JUMP_SPEED;
					}
					if (player && (compatflags & COMPATF_WALLRUN))
					{
					// [RH] Here is the key to wall running: The move is clipped using its full speed.
					// If the move is done a second time (because it was too fast for one move), it
					// is still clipped against the wall at its full speed, so you effectively
					// execute two moves in one tic.
						P_SlideMove (mo, mo->momx, mo->momy, 1);
					}
					else
					{
						P_SlideMove (mo, onestepx, onestepy, totalsteps);
					}
					if ((mo->momx | mo->momy) == 0)
					{
						steps = 0;
					}
					else
					{
						if (!player || !(compatflags & COMPATF_WALLRUN))
						{
							xmove = mo->momx;
							ymove = mo->momy;
							onestepx = xmove / steps;
							onestepy = ymove / steps;
							P_CheckSlopeWalk (mo, xmove, ymove);
						}
						startx = mo->x - Scale (xmove, step, steps);
						starty = mo->y - Scale (ymove, step, steps);
					}
				}
				else
				{ // slide against another actor
					fixed_t tx, ty;
					tx = 0, ty = onestepy;
					walkplane = P_CheckSlopeWalk (mo, tx, ty);
					if (P_TryMove (mo, mo->x + tx, mo->y + ty, true, walkplane))
					{
						mo->momx = 0;
					}
					else
					{
						tx = onestepx, ty = 0;
						walkplane = P_CheckSlopeWalk (mo, tx, ty);
						if (P_TryMove (mo, mo->x + tx, mo->y + ty, true, walkplane))
						{
							mo->momy = 0;
						}
						else
						{
							mo->momx = mo->momy = 0;
						}
					}
					if (player && player->mo == mo)
					{
						if (mo->momx == 0)
							player->momx = 0;
						if (mo->momy == 0)
							player->momy = 0;
					}
					steps = 0;
				}
			}
			else if (mo->flags & MF_MISSILE)
			{
				steps = 0;
				if (BlockingMobj)
				{
					if (mo->flags2 & MF2_BOUNCE2)
					{
						if ((BlockingMobj->flags2 & MF2_REFLECTIVE) ||
							((!BlockingMobj->player) &&
							(!(BlockingMobj->flags3 & MF3_ISMONSTER))))
						{
							fixed_t speed;

							angle = R_PointToAngle2 (BlockingMobj->x,
								BlockingMobj->y, mo->x, mo->y)
								+ANGLE_1*((pr_bounce()%16)-8);
							speed = P_AproxDistance (mo->momx, mo->momy);
							speed = FixedMul (speed, (fixed_t)(0.75*FRACUNIT));
							mo->angle = angle;
							angle >>= ANGLETOFINESHIFT;
							mo->momx = FixedMul (speed, finecosine[angle]);
							mo->momy = FixedMul (speed, finesine[angle]);
							if (mo->SeeSound)
							{
								S_SoundID (mo, CHAN_VOICE, mo->SeeSound, 1, ATTN_IDLE);
							}
							DoRipping = false;
							return;
						}
						else
						{ // Struck a player/creature
							P_ExplodeMissile (mo, NULL);
							DoRipping = false;
							return;
						}
					}
				}
				else
				{
					// Struck a wall
					if (P_BounceWall (mo))
					{
						if (mo->SeeSound && !(mo->flags3 & MF3_NOWALLBOUNCESND))
						{
							S_SoundID (mo, CHAN_VOICE, mo->SeeSound, 1, ATTN_IDLE);
						}
						DoRipping = false;
						return;
					}
				}
				if (BlockingMobj &&
					(BlockingMobj->flags2 & MF2_REFLECTIVE))
				{
					angle = R_PointToAngle2(BlockingMobj->x,
													BlockingMobj->y,
													mo->x, mo->y);

				// Change angle for deflection/reflection
					if (mo->AdjustReflectionAngle (BlockingMobj, angle))
					{
						goto explode;
					}

					// Reflect the missile along angle
					mo->angle = angle;
					angle >>= ANGLETOFINESHIFT;
					mo->momx = FixedMul (mo->Speed>>1, finecosine[angle]);
					mo->momy = FixedMul (mo->Speed>>1, finesine[angle]);
					mo->momz = -mo->momz/2;
					if (mo->flags2 & MF2_SEEKERMISSILE)
					{
						mo->tracer = mo->target;
					}
					mo->target = BlockingMobj;
					DoRipping = false;
					return;
				}
explode:
				// explode a missile
				if (ceilingline &&
					ceilingline->backsector &&
					ceilingline->backsector->ceilingpic == skyflatnum &&
					mo->z >= ceilingline->backsector->ceilingplane.ZatPoint (mo->x, mo->y) && //killough
					!(mo->flags3 & MF3_SKYEXPLODE))
				{
					// Hack to prevent missiles exploding against the sky.
					// Does not handle sky floors.
					mo->Destroy ();
					DoRipping = false;
					return;
				}
				// [RH] Don't explode on horizon lines.
				if (BlockingLine != NULL && BlockingLine->special == Line_Horizon)
				{
					mo->Destroy ();
					DoRipping = false;
					return;
				}
				P_ExplodeMissile (mo, BlockingLine);
				DoRipping = false;
				return;
			}
			else
			{
				mo->momx = mo->momy = 0;
				steps = 0;
			}
		}
		else
		{
			if (mo->x != ptryx || mo->y != ptryy)
			{
				// If the new position does not match the desired position, the player
				// must have gone through a teleporter, so stop moving right now if it
				// was a regular teleporter. If it was a line-to-line or fogless teleporter,
				// the move should continue, but startx and starty need to change.
				if (mo->momx == 0 && mo->momy == 0)
				{
					step = steps;
				}
				else
				{
					startx = mo->x - Scale (xmove, step, steps);
					starty = mo->y - Scale (ymove, step, steps);
				}
			}
		}
	} while (++step <= steps);

	DoRipping = false;

	// Friction

	if (player && player->mo == mo && player->cheats & CF_NOMOMENTUM)
	{ // debug option for no sliding at all
		mo->momx = mo->momy = 0;
		player->momx = player->momy = 0;
		return;
	}

	if (mo->flags & (MF_MISSILE | MF_SKULLFLY))
	{ // no friction for missiles
		return;
	}

	if (mo->z > mo->floorz && !(mo->flags2 & MF2_ONMOBJ) &&
		(!(mo->flags2 & MF2_FLY) || !(mo->flags & MF_NOGRAVITY)) && !mo->waterlevel)
	{ // [RH] Friction when falling is available for larger aircontrols
		if (player != NULL && level.airfriction != FRACUNIT)
		{
			mo->momx = FixedMul (mo->momx, level.airfriction);
			mo->momy = FixedMul (mo->momy, level.airfriction);

			if (player->mo == mo)		//  Not voodoo dolls
			{
				player->momx = FixedMul (player->momx, level.airfriction);
				player->momy = FixedMul (player->momy, level.airfriction);
			}
		}
		return;
	}

	if (mo->flags & MF_CORPSE)
	{ // Don't stop sliding if halfway off a step with some momentum
		if (mo->momx > FRACUNIT/4 || mo->momx < -FRACUNIT/4
			|| mo->momy > FRACUNIT/4 || mo->momy < -FRACUNIT/4)
		{
			if (mo->floorz > mo->Sector->floorplane.ZatPoint (mo->x, mo->y))
				return;
		}
	}

	// killough 11/98:
	// Stop voodoo dolls that have come to rest, despite any
	// moving corresponding player:
	if (mo->momx > -STOPSPEED && mo->momx < STOPSPEED
		&& mo->momy > -STOPSPEED && mo->momy < STOPSPEED
		&& (!player || (player->mo != mo)
			|| !(player->cmd.ucmd.forwardmove | player->cmd.ucmd.sidemove)))
	{
		// if in a walking frame, stop moving
		// killough 10/98:
		// Don't affect main player when voodoo dolls stop:
		if (player && player->mo == mo && !(player->cheats & CF_PREDICTING))
		{
			player->mo->PlayIdle ();
		}

		mo->momx = mo->momy = 0;

		// killough 10/98: kill any bobbing momentum too (except in voodoo dolls)
		if (player && player->mo == mo)
			player->momx = player->momy = 0; 
	}
	else
	{
		// phares 3/17/98
		// Friction will have been adjusted by friction thinkers for icy
		// or muddy floors. Otherwise it was never touched and
		// remained set at ORIG_FRICTION
		//
		// killough 8/28/98: removed inefficient thinker algorithm,
		// instead using touching_sectorlist in P_GetFriction() to
		// determine friction (and thus only when it is needed).
		//
		// killough 10/98: changed to work with new bobbing method.
		// Reducing player momentum is no longer needed to reduce
		// bobbing, so ice works much better now.

		fixed_t friction = P_GetFriction (mo, NULL);

		mo->momx = FixedMul (mo->momx, friction);
		mo->momy = FixedMul (mo->momy, friction);

		// killough 10/98: Always decrease player bobbing by ORIG_FRICTION.
		// This prevents problems with bobbing on ice, where it was not being
		// reduced fast enough, leading to all sorts of kludges being developed.

		if (player && player->mo == mo)		//  Not voodoo dolls
		{
			player->momx = FixedMul (player->momx, ORIG_FRICTION);
			player->momy = FixedMul (player->momy, ORIG_FRICTION);
		}
	}
}

// Move this to p_inter ***
void P_MonsterFallingDamage (AActor *mo)
{
	int damage;
	int mom;

	if (gameinfo.gametype != GAME_Hexen)
		return;

	mom = abs(mo->momz);
	if (mom > 35*FRACUNIT)
	{ // automatic death
		damage = 1000000;
	}
	else
	{
		damage = ((mom - (23*FRACUNIT))*6)>>FRACBITS;
	}
	damage = 1000000;	// always kill 'em
	P_DamageMobj (mo, NULL, NULL, damage, MOD_FALLING);
}

//
// P_ZMovement
//
void P_ZMovement (AActor *mo)
{
	fixed_t dist;
	fixed_t delta;
	fixed_t oldz = mo->z;

//	
// check for smooth step up
//
	if (mo->player && mo->player->mo == mo && mo->z < mo->floorz)
	{
		mo->player->viewheight -= mo->floorz - mo->z;
		mo->player->deltaviewheight = (VIEWHEIGHT - mo->player->viewheight)>>3;
	}
//
// apply gravity
//
	if (mo->z > mo->floorz && !(mo->flags & MF_NOGRAVITY))
	{
		fixed_t startmomz = mo->momz;

		if (!mo->waterlevel || mo->flags & MF_CORPSE || (mo->player &&
			!(mo->player->cmd.ucmd.forwardmove | mo->player->cmd.ucmd.sidemove)))
		{
			mo->momz -= (fixed_t)(level.gravity * mo->Sector->gravity *
				(mo->flags2 & MF2_LOGRAV ? 10.24 : 81.92));
		}
		if (mo->waterlevel > 1)
		{
			fixed_t sinkspeed = mo->flags & MF_CORPSE ? -WATER_SINK_SPEED/3 : -WATER_SINK_SPEED;

			if (mo->momz < sinkspeed)
			{
				mo->momz = (startmomz < sinkspeed) ? startmomz : sinkspeed;
			}
			else
			{
				mo->momz = startmomz + ((mo->momz - startmomz) >>
					(mo->waterlevel == 1 ? WATER_SINK_SMALL_FACTOR : WATER_SINK_FACTOR));
			}
		}
	}
//
// adjust height
//
	mo->z += mo->momz;
	if ((mo->flags & MF_FLOAT) && mo->target)
	{	// float down towards target if too close
		if (!(mo->flags & MF_SKULLFLY) && !(mo->flags & MF_INFLOAT))
		{
			dist = P_AproxDistance (mo->x - mo->target->x, mo->y - mo->target->y);
			delta = (mo->target->z + (mo->height>>1)) - mo->z;
			if (delta < 0 && dist < -(delta*3))
				mo->z -= FLOATSPEED, mo->momz = 0;
			else if (delta > 0 && dist < (delta*3))
				mo->z += FLOATSPEED, mo->momz = 0;
		}
	}
	if (mo->player && (mo->flags & MF_NOGRAVITY) && (mo->z > mo->floorz))
	{
		mo->z += finesine[(FINEANGLES/80*level.time)&FINEMASK]/8;
		mo->momz = FixedMul (mo->momz, FRICTION_FLY);
	}
	if (mo->waterlevel && !(mo->flags & MF_NOGRAVITY))
	{
		mo->momz = FixedMul (mo->momz, mo->Sector->friction);
	}

//
// clip movement
//
	if (mo->z <= mo->floorz)
	{	// Hit the floor
		if ((!mo->player || !(mo->player->cheats & CF_PREDICTING)) &&
			mo->Sector->SecActTarget != NULL &&
			mo->Sector->floorplane.ZatPoint (mo->x, mo->y) == mo->floorz)
		{ // [RH] Let the sector do something to the actor
			mo->Sector->SecActTarget->TriggerAction (mo, SECSPAC_HitFloor);
		}
		// [RH] Need to recheck this because the sector action might have
		// teleported the actor so it is no longer below the floor.
		if (mo->z <= mo->floorz)
		{
			if ((mo->flags & MF_MISSILE) &&
				(gameinfo.gametype != GAME_Doom || !(mo->flags & MF_NOCLIP)))
			{
				mo->z = mo->floorz;
				if (mo->flags2 & MF2_BOUNCETYPE)
				{
					mo->FloorBounceMissile (mo->floorsector->floorplane);
					return;
				}
				else if (mo->flags3 & MF3_NOEXPLODEFLOOR)
				{
					mo->momz = 0;
					P_HitFloor (mo);
					return;
				}
				else if (mo->flags3 & MF3_FLOORHUGGER)
				{ // Floor huggers can go up steps
					return;
				}
				else
				{
					if (mo->floorsector->floorpic == skyflatnum &&
						!(mo->flags3 & MF3_SKYEXPLODE))
					{
						// [RH] Just remove the missile without exploding it
						//		if this is a sky floor.
						mo->Destroy ();
						return;
					}
					P_HitFloor (mo);
					P_ExplodeMissile (mo, NULL);
					return;
				}
			}
			if (mo->flags3 & MF3_ISMONSTER)		// Blasted mobj falling
			{
				if (mo->momz < -(23*FRACUNIT))
				{
					P_MonsterFallingDamage (mo);
				}
			}
			mo->z = mo->floorz;
			if (mo->momz < 0)
			{
				// [RH] avoid integer roundoff by doing comparisons with floats
				// I can't think of any good reason why this varied with gravity
				float minmom = 800.f /*level.gravity * mo->Sector->gravity*/ * -655.36f;
				float mom = (float)mo->momz;

				// Spawn splashes, etc.
				P_HitFloor (mo);
				if (mo->DamageType == MOD_ICE && mom < minmom)
				{
					mo->tics = 1;
					mo->momx = 0;
					mo->momy = 0;
					mo->momz = 0;
					return;
				}
				// Let the actor do something special for hitting the floor
				mo->HitFloor ();
				if (mo->player)
				{
					mo->player->jumpTics = 7;	// delay any jumping for a short while
					if (mom < minmom && !(mo->flags & MF_NOGRAVITY))
					{
						// Squat down.
						// Decrease viewheight for a moment after hitting the ground (hard),
						// and utter appropriate sound.
						PlayerLandedOnThing (mo, NULL);
					}
				}
				mo->momz = 0;
			}
			if (mo->flags & MF_SKULLFLY)
			{ // The skull slammed into something
				mo->momz = -mo->momz;
			}
			if (mo->CrashState &&
				(mo->flags & MF_CORPSE) &&
				!(mo->flags3 & MF3_CRASHED) &&
				mo->DamageType != MOD_ICE)
			{
				mo->flags3 |= MF3_CRASHED;
				mo->SetState (mo->CrashState);
			}
		}
	}

	if (mo->flags2 & MF2_FLOORCLIP)
	{
		mo->AdjustFloorClip ();
	}

	if (mo->z + mo->height > mo->ceilingz)
	{ // hit the ceiling
		if ((!mo->player || !(mo->player->cheats & CF_PREDICTING)) &&
			mo->Sector->SecActTarget != NULL &&
			mo->Sector->ceilingplane.ZatPoint (mo->x, mo->y) == mo->ceilingz)
		{ // [RH] Let the sector do something to the actor
			mo->Sector->SecActTarget->TriggerAction (mo, SECSPAC_HitCeiling);
		}
		// [RH] Need to recheck this because the sector action might have
		// teleported the actor so it is no longer above the ceiling.
		if (mo->z + mo->height > mo->ceilingz)
		{
			mo->z = mo->ceilingz - mo->height;
			if (mo->flags2 & MF2_BOUNCETYPE)
			{	// ceiling bounce
				mo->FloorBounceMissile (mo->Sector->ceilingplane);
				return;
			}
			if (mo->momz > 0)
				mo->momz = 0;
			if (mo->flags & MF_SKULLFLY)
			{	// the skull slammed into something
				mo->momz = -mo->momz;
			}
			if (mo->flags & MF_MISSILE &&
				(gameinfo.gametype != GAME_Doom || !(mo->flags & MF_NOCLIP)))
			{
				if (mo->flags3 & MF3_CEILINGHUGGER)
				{
					return;
				}
				if (!(mo->flags3 & MF3_SKYEXPLODE) &&
					mo->Sector->ceilingpic == skyflatnum)
				{
					mo->Destroy ();
					return;
				}
				P_ExplodeMissile (mo, NULL);
				return;
			}
		}
	}
	P_CheckFakeFloorTriggers (mo, oldz);
}

void P_CheckFakeFloorTriggers (AActor *mo, fixed_t oldz)
{
	if (mo->player && (mo->player->cheats & CF_PREDICTING))
	{
		return;
	}
	sector_t *sec = mo->Sector;
	if (sec->heightsec != NULL && sec->SecActTarget != NULL)
	{
		sector_t *hs = sec->heightsec;
		fixed_t waterz = hs->floorplane.ZatPoint (mo->x, mo->y);
		fixed_t newz;
		fixed_t viewheight;

		if (mo->player != NULL)
		{
			viewheight = mo->player->viewheight;
		}
		else
		{
			viewheight = mo->height / 2;
		}

		if (oldz > waterz && mo->z <= waterz)
		{ // Feet hit fake floor
			sec->SecActTarget->TriggerAction (mo, SECSPAC_HitFakeFloor);
		}

		newz = mo->z + viewheight;
		oldz += viewheight;

		if (oldz <= waterz && newz > waterz)
		{ // View went above fake floor
			sec->SecActTarget->TriggerAction (mo, SECSPAC_EyesSurface);
		}
		else if (oldz > waterz && newz <= waterz)
		{ // View went below fake floor
			sec->SecActTarget->TriggerAction (mo, SECSPAC_EyesDive);
		}

		if (!(hs->MoreFlags & SECF_FAKEFLOORONLY))
		{
			waterz = hs->ceilingplane.ZatPoint (mo->x, mo->y);
			if (oldz <= waterz && newz > waterz)
			{ // View went above fake ceiling
				sec->SecActTarget->TriggerAction (mo, SECSPAC_EyesAboveC);
			}
			else if (oldz > waterz && newz <= waterz)
			{ // View went below fake ceiling
				sec->SecActTarget->TriggerAction (mo, SECSPAC_EyesBelowC);
			}
		}
	}
}

//===========================================================================
//
// PlayerLandedOnThing
//
//===========================================================================

static void PlayerLandedOnThing (AActor *mo, AActor *onmobj)
{
	bool grunted;

	if (!mo->player)
		return;

	if (mo->player->mo == mo)
	{
		mo->player->deltaviewheight = mo->momz>>3;
	}

	if (mo->player->cheats & CF_PREDICTING)
		return;

	P_FallingDamage (mo);

	// [RH] only make noise if alive
	if (!mo->player->morphTics && mo->health > 0)
	{
		grunted = false;
		// Why should this number vary by gravity?
		if (mo->momz < (fixed_t)(800.f /*level.gravity * mo->Sector->gravity*/ * -983.04f) && mo->health > 0)
		{
			S_Sound (mo, CHAN_VOICE, "*grunt", 1, ATTN_NORM);
			grunted = true;
		}
		if (onmobj != NULL || !Terrains[P_GetThingFloorType (mo)].IsLiquid)
		{
			if (!grunted || !S_AreSoundsEquivalent (mo, "*grunt", "*land"))
			{
				S_Sound (mo, CHAN_AUTO, "*land", 1, ATTN_NORM);
			}
		}
	}
//	mo->player->centering = true;
}



//
// P_NightmareRespawn
//
void P_NightmareRespawn (AActor *mobj)
{
	fixed_t x, y, z;
	AActor *mo;
	AActor *info = mobj->GetDefault();

	// spawn the new monster (assume the spawn will be good)
	if (info->flags & MF_SPAWNCEILING)
		z = ONCEILINGZ;
	else if (info->flags2 & MF2_SPAWNFLOAT)
		z = FLOATRANDZ;
	else if (info->flags2 & MF2_FLOATBOB)
		z = mobj->SpawnPoint[2] << FRACBITS;
	else
		z = ONFLOORZ;

	// spawn it
	x = mobj->SpawnPoint[0] << FRACBITS;
	y = mobj->SpawnPoint[1] << FRACBITS;
	mo = Spawn (RUNTIME_TYPE(mobj), x, y, z);

	if (z == ONFLOORZ)
		mo->z += mo->SpawnPoint[2] << FRACBITS;
	else if (z == ONCEILINGZ)
		mo->z -= mo->SpawnPoint[2] << FRACBITS;

	// something is occupying its position?
	if (!P_TestMobjLocation (mo))
	{
		level.total_monsters--;
		mo->Destroy ();
		return;		// no respawn
	}

	z = mo->z;

	// inherit attributes from deceased one
	mo->SpawnPoint[0] = mobj->SpawnPoint[0];
	mo->SpawnPoint[1] = mobj->SpawnPoint[1];
	mo->SpawnPoint[2] = mobj->SpawnPoint[2];
	mo->SpawnAngle = mobj->SpawnAngle;
	mo->SpawnFlags = mobj->SpawnFlags;
	mo->angle = ANG45 * (mobj->SpawnAngle/45);

	mo->HandleSpawnFlags ();
	mo->reactiontime = 18;
	mo->CopyFriendliness (mobj, false);
	mo->Translation = mobj->Translation;

	// spawn a teleport fog at old spot because of removal of the body?
	mo = Spawn ("TeleportFog", mobj->x, mobj->y, mobj->z);
	if (mo != NULL)
	{
		mo->z += TELEFOGHEIGHT;
	}

	// spawn a teleport fog at the new spot
	mo = Spawn ("TeleportFog", x, y, z);
	if (mo != NULL)
	{
		mo->z += TELEFOGHEIGHT;
	}

	// remove the old monster
	mobj->Destroy ();
}


//
// [RH] Some new functions to work with Thing IDs. ------->
//
AActor *AActor::TIDHash[128];

//
// P_ClearTidHashes
//
// Clears the tid hashtable.
//

void AActor::ClearTIDHashes ()
{
	int i;

	for (i = 0; i < 128; i++)
		TIDHash[i] = NULL;
}

//
// P_AddMobjToHash
//
// Inserts an mobj into the correct chain based on its tid.
// If its tid is 0, this function does nothing.
//
void AActor::AddToHash ()
{
	if (tid == 0)
	{
		iprev = NULL;
		inext = NULL;
		return;
	}
	else
	{
		int hash = TIDHASH (tid);

		inext = TIDHash[hash];
		iprev = &TIDHash[hash];
		TIDHash[hash] = this;
		if (inext)
		{
			inext->iprev = &inext;
		}
	}
}

//
// P_RemoveMobjFromHash
//
// Removes an mobj from its hash chain.
//
void AActor::RemoveFromHash ()
{
	if (tid != 0 && iprev)
	{
		*iprev = inext;
		if (inext)
		{
			inext->iprev = iprev;
		}
		iprev = NULL;
		inext = NULL;
	}
	tid = 0;
}

// <------- [RH] End new functions

angle_t AActor::AngleIncrements ()
{
	return ANGLE_45;
}

const char *AActor::GetObituary ()
{
	return NULL;
}

const char *AActor::GetHitObituary ()
{
	return GetObituary ();
}

void AActor::PreExplode ()
{
}

void AActor::GetExplodeParms (int &damage, int &dist, bool &hurtSource)
{
}

void AActor::Howl ()
{
}

void AActor::NoBlockingSet ()
{
}

fixed_t AActor::GetSinkSpeed ()
{
	return FRACUNIT;
}

fixed_t AActor::GetRaiseSpeed ()
{
	return 2*FRACUNIT;
}

void AActor::HitFloor ()
{
}

bool AActor::Slam (AActor *thing)
{
	int dam = ((pr_slam()%8)+1) * damage;
	P_DamageMobj (thing, this, this, dam, MOD_HIT);
	P_TraceBleed (dam, thing, this);
	flags &= ~MF_SKULLFLY;
	momx = momy = momz = 0;
	SetState (SeeState != NULL ? SeeState : SpawnState);
	return false;			// stop moving
}

bool AActor::SpecialBlastHandling (AActor *source, fixed_t strength)
{
	return true;
}

int AActor::SpecialMissileHit (AActor *victim)
{
	return -1;
}

bool AActor::AdjustReflectionAngle (AActor *thing, angle_t &angle)
{
	angle += ANGLE_1 * ((pr_reflect()%16)-8);
	return false;
}

void AActor::PlayActiveSound ()
{
	if (ActiveSound && !S_IsActorPlayingSomething (this, CHAN_VOICE))
	{
		S_SoundID (this, CHAN_VOICE, ActiveSound, 1,
			(flags3 & MF3_FULLVOLACTIVE) ? ATTN_NONE : ATTN_IDLE);
	}
}

bool AActor::IsOkayToAttack (AActor *link)
{
	if (player)				// Minotaur looking around player
	{
		if ((link->flags3 & MF3_ISMONSTER) ||
			(link->player && (link != this)))
		{
			if (!(link->flags&MF_SHOOTABLE))
			{
				return false;
			}
			if (link->flags2&MF2_DORMANT)
			{
				return false;
			}
			if ((link->IsKindOf (RUNTIME_CLASS(AMinotaur))) &&
				(link->tracer == this))
			{
				return false;
			}
			if (multiplayer && !deathmatch && link->player)
			{
				return false;
			}
			if (P_CheckSight (this, link))
			{
				return true;
			}
		}
	}
	return false;
}

void AActor::ChangeSpecial (byte special, byte data1, byte data2,
	byte data3, byte data4, byte data5)
{
	this->special = special;
	args[0] = data1;
	args[1] = data2;
	args[2] = data3;
	args[3] = data4;
	args[4] = data5;
}

void AActor::SetShade (DWORD rgb)
{
	PalEntry *entry = (PalEntry *)&rgb;
	alphacolor = rgb | (ColorMatcher.Pick (entry->r, entry->g, entry->b) << 24);
}

void AActor::SetShade (int r, int g, int b)
{
	alphacolor = MAKEARGB(ColorMatcher.Pick (r, g, b), r, g, b);
}

//
// P_MobjThinker
//
void AActor::Tick ()
{
	// [RH] Data for Heretic/Hexen scrolling sectors
	static const byte HexenScrollDirs[8] = { 64, 0, 192, 128, 96, 32, 224, 160 };
	static const char HexenSpeedMuls[3] = { 5, 10, 25 };

	static const byte HereticScrollDirs[4] = { 6, 9, 1, 4 };
	static const char HereticSpeedMuls[5] = { 5, 10, 25, 30, 35 };

	AActor *onmo;
	int i;

	PrevX = x;
	PrevY = y;
	PrevZ = z;

	if (flags & MF_UNMORPHED)
	{
		return;
	}

	//Added by MC: Freeze mode.
	if (bglobal.freeze && !(player && !player->isbot))
	{
		return;
	}

	fixed_t oldz = z;

	// [RH] Pulse in and out of visibility
	if (effects & FX_VISIBILITYPULSE)
	{
		if (visdir > 0)
		{
			alpha += 0x800;
			if (alpha >= OPAQUE)
			{
				alpha = OPAQUE;
				visdir = -1;
			}
		}
		else
		{
			alpha -= 0x800;
			if (alpha <= TRANSLUC25)
			{
				alpha = TRANSLUC25;
				visdir = 1;
			}
		}
	}
	else if (flags & MF_STEALTH)
	{
		// [RH] Fade a stealth monster in and out of visibility
		if (visdir > 0)
		{
			alpha += 2*FRACUNIT/TICRATE;
			if (alpha > OPAQUE)
			{
				alpha = OPAQUE;
				visdir = 0;
			}
		}
		else if (visdir < 0)
		{
			alpha -= 3*FRACUNIT/TICRATE/2;
			if (alpha < 0)
			{
				alpha = 0;
				visdir = 0;
			}
		}
	}

	if (bglobal.botnum && consoleplayer == Net_Arbitrator && !demoplayback &&
		(flags & (MF_SPECIAL|MF_MISSILE)) || (flags3 & MF3_ISMONSTER))
	{
		clock (BotSupportCycles);
		bglobal.m_Thinking = true;
		for (i = 0; i < MAXPLAYERS; i++)
		{
			if (!playeringame[i] || !players[i].isbot)
				continue;

			if (flags3 & MF3_ISMONSTER)
			{
				if (health > 0
					&& !players[i].enemy
					&& player ? !IsTeammate (players[i].mo) : true
					&& P_AproxDistance (players[i].mo->x-x, players[i].mo->y-y) < MAX_MONSTER_TARGET_DIST
					&& P_CheckSight (players[i].mo, this, 2))
				{ //Probably a monster, so go kill it.
					players[i].enemy = this;
				}
			}
			else if (flags & MF_SPECIAL)
			{ //Item pickup time
				//clock (BotWTG);
				bglobal.WhatToGet (players[i].mo, this);
				//unclock (BotWTG);
				BotWTG++;
			}
			else if (flags & MF_MISSILE)
			{
				if (!players[i].missile && (flags3 & MF3_WARNBOT))
				{ //warn for incoming missiles.
					if (target != players[i].mo && bglobal.Check_LOS (players[i].mo, this, ANGLE_90))
						players[i].missile = this;
				}
			}
		}
		bglobal.m_Thinking = false;
		unclock (BotSupportCycles);
	}

	//End of MC

	// [RH] Consider carrying sectors here
	fixed_t cummx = 0, cummy = 0;
	if ((level.Scrolls != NULL || player != NULL) && !(flags & MF_NOCLIP) && !(flags & MF_NOSECTOR))
	{
		fixed_t height, waterheight;	// killough 4/4/98: add waterheight
		const msecnode_t *node;
		int countx, county;

		// killough 3/7/98: Carry things on floor
		// killough 3/20/98: use new sector list which reflects true members
		// killough 3/27/98: fix carrier bug
		// killough 4/4/98: Underwater, carry things even w/o gravity

		// Move objects only if on floor or underwater,
		// non-floating, and clipped.

		countx = county = 0;

		for (node = touching_sectorlist; node; node = node->m_tnext)
		{
			const sector_t *sec = node->m_sector;
			fixed_t scrollx, scrolly;

			if (level.Scrolls != NULL)
			{
				const FSectorScrollValues *scroll = &level.Scrolls[sec - sectors];
				scrollx = scroll->ScrollX;
				scrolly = scroll->ScrollY;
			}
			else
			{
				scrollx = scrolly = 0;
			}

			if (player != NULL)
			{
				int scrolltype = sec->special & 0xff;

				if (scrolltype >= Scroll_North_Slow &&
					scrolltype <= Scroll_SouthWest_Fast)
				{ // Hexen scroll special
					scrolltype -= Scroll_North_Slow;
					angle_t fineangle = HexenScrollDirs[scrolltype / 3] * 32;
					fixed_t carryspeed = HexenSpeedMuls[scrolltype % 3] * 2048;
					scrollx += FixedMul (carryspeed, finecosine[fineangle]);
					scrolly += FixedMul (carryspeed, finesine[fineangle]);
				}
				else if (scrolltype >= Carry_East5 &&
						 scrolltype <= Carry_West35)
				{ // Heretic scroll special
					scrolltype -= Carry_East5;
					byte dir = HereticScrollDirs[scrolltype / 5];
					fixed_t carryspeed = HereticSpeedMuls[scrolltype % 5] * 2048;
					scrollx += carryspeed * ((dir & 3) - 1);
					scrolly += carryspeed * (((dir & 12) >> 2) - 1);
				}
				else if (scrolltype == dScroll_EastLavaDamage)
				{ // Special Heretic scroll special
					scrollx += 2048*28;
				}
				else if (scrolltype == Scroll_StrifeCurrent)
				{ // Strife scroll special
					int anglespeed = sec->tag - 100;
					fixed_t carryspeed = (anglespeed % 10) << (FRACBITS - 4);
					angle_t fineangle = (anglespeed / 10) << (32-3);
					fineangle >>= ANGLETOFINESHIFT;
					scrollx += FixedMul (carryspeed, finecosine[fineangle]);
					scrolly += FixedMul (carryspeed, finesine[fineangle]);
				}
			}

			if ((scrollx | scrolly) == 0)
			{
				continue;
			}
			if (flags & MF_NOGRAVITY &&
				(sec->heightsec == NULL || (sec->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC)))
			{
				continue;
			}
			height = sec->floorplane.ZatPoint (x, y);
			if (z > height)
			{
				if (sec->heightsec == NULL || (sec->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC))
				{
					continue;
				}

				waterheight = sec->heightsec->floorplane.ZatPoint (x, y);
				if (waterheight > height && z >= waterheight)
				{
					continue;
				}
			}

			cummx += scrollx;
			cummy += scrolly;
			if (scrollx) countx++;
			if (scrolly) county++;
		}

		// Some levels designed with Boom in mind actually want things to accelerate
		// at neighboring scrolling sector boundaries. But it is only important for
		// non-player objects.
		if (player != NULL || !(level.flags & LEVEL_ADDITIVE_SCROLLERS))
		{
			if (countx > 1)
			{
				cummx /= countx;
			}
			if (county > 1)
			{
				cummy /= county;
			}
		}
	}

	// [RH] If standing on a steep slope, fall down it
	if ((flags & MF_SOLID) && !(flags & (MF_NOCLIP|MF_NOGRAVITY)) &&
		!(flags & MF_NOBLOCKMAP) &&
		momz <= 0 &&
		floorz == z &&
		floorsector->floorplane.c < STEEPSLOPE &&
		floorsector->floorplane.ZatPoint (x, y) <= floorz)
	{
		const msecnode_t *node;
		bool dopush = true;

		if (floorsector->floorplane.c > STEEPSLOPE*2/3)
		{
			for (node = touching_sectorlist; node; node = node->m_tnext)
			{
				const sector_t *sec = node->m_sector;
				if (sec->floorplane.c >= STEEPSLOPE)
				{
					if (sec->floorplane.ZatPoint (x, y) >= z - gameinfo.StepHeight)
					{
						dopush = false;
						break;
					}
				}
			}
		}
		if (dopush)
		{
			momx += floorsector->floorplane.a;
			momy += floorsector->floorplane.b;
		}
	}

	// [RH] Missiles moving perfectly vertical need some X/Y movement, or they
	// won't hurt anything. Don't do this if damage is 0! That way, you can
	// still have missiles that go straight up and down through actors without
	// damaging anything.
	if ((flags & MF_MISSILE) && (momx|momy) == 0 && damage == 0)
	{
		momx = 1;
	}

	// Handle X and Y momemtums
	BlockingMobj = NULL;
	P_XYMovement (this, cummx, cummy);
	if (ObjectFlags & OF_MassDestruction)
	{ // actor was destroyed
		return;
	}
	if ((momx | momy) == 0 && (flags2 & MF2_BLASTED))
	{ // Reset to not blasted when momentums are gone
		flags2 &= ~MF2_BLASTED;
		if (!(flags & MF_ICECORPSE))
		{
			flags2 &= ~MF2_SLIDE;
		}
	}

	if (flags2 & MF2_FLOATBOB)
	{ // Floating item bobbing motion
		z += FloatBobDiffs[(FloatBobPhase + level.time) & 63];
	}
	if (momz || BlockingMobj ||
		(z != floorz && (!(flags2 & MF2_FLOATBOB) ||
		(z - FloatBobOffsets[(FloatBobPhase + level.time) & 63] != floorz)
		)))
	{	// Handle Z momentum and gravity
		if (((flags2 & MF2_PASSMOBJ) || (flags & MF_SPECIAL)) && !(compatflags & COMPATF_NO_PASSMOBJ))
		{
			if (!(onmo = P_CheckOnmobj (this)))
			{
				P_ZMovement (this);
				flags2 &= ~MF2_ONMOBJ;
			}
			else
			{
				if (player)
				{
					if (momz < (fixed_t)(level.gravity * Sector->gravity * -655.36f)
						&& !(flags&MF_NOGRAVITY))
					{
						PlayerLandedOnThing (this, onmo);
					}
				}
				if (onmo->z + onmo->height - z <= gameinfo.StepHeight)
				{
					if (player && player->mo == this)
					{
						player->viewheight -= onmo->z + onmo->height - z;
						fixed_t deltaview = (VIEWHEIGHT - player->viewheight)>>3;
						if (deltaview > player->deltaviewheight)
						{
							player->deltaviewheight = deltaview;
						}
					} 
					z = onmo->z + onmo->height;
				}
				flags2 |= MF2_ONMOBJ;
				momz = 0;
				if (CrashState &&
					(flags & MF_CORPSE) &&
					!(flags3 & MF3_CRASHED) &&
					DamageType != MOD_ICE)
				{
					flags3 |= MF3_CRASHED;
					SetState (CrashState);
				}
			}
		}
		else
		{
			P_ZMovement (this);
		}

		if (ObjectFlags & OF_MassDestruction)
			return;		// actor was destroyed
	}
	else if (z <= floorz)
	{
		if (CrashState &&
			(flags & MF_CORPSE) &&
			!(flags3 & MF3_CRASHED) &&
			DamageType != MOD_ICE)
		{
			flags3 |= MF3_CRASHED;
			SetState (CrashState);
		}
	}

	if (UpdateWaterLevel (oldz))
	{
		P_HitWater (this, Sector);
	}

	// [RH] Don't advance if predicting a player
	if (player && (player->cheats & CF_PREDICTING))
	{
		return;
	}

	// cycle through states, calling action functions at transitions
	if (tics != -1)
	{
		tics--;
				
		// you can cycle through multiple states in a tic
		// [RH] Use <= 0 instead of == 0 so that spawnstates
		// of 0 tics work as expected.
		if (tics <= 0)
		{
			if (!SetState (state->GetNextState()))
				return; 		// freed itself
		}
	}
	else
	{
		// check for nightmare respawn
		if (!respawnmonsters || !(flags3 & MF3_ISMONSTER) || (flags2 & MF2_DORMANT))
			return;

		movecount++;

		if (movecount < respawnmonsters)
			return;

		if (level.time & 31)
			return;

		if (pr_nightmarerespawn() > 4)
			return;

		P_NightmareRespawn (this);
	}
}

//==========================================================================
//
// AActor::UpdateWaterLevel
//
// Returns true if actor should splash
//
//==========================================================================

bool AActor::UpdateWaterLevel (fixed_t oldz)
{
	byte lastwaterlevel = waterlevel;

	waterlevel = 0;

	if (Sector == NULL)
	{
		return false;
	}

	if (Sector->MoreFlags & SECF_UNDERWATERMASK)
	{
		waterlevel = 3;
	}
	else
	{
		const sector_t *hsec = Sector->heightsec;
		if (hsec != NULL && !(hsec->MoreFlags & SECF_IGNOREHEIGHTSEC))
		{
			fixed_t fh = hsec->floorplane.ZatPoint (x, y);
			if (hsec->MoreFlags & SECF_UNDERWATERMASK)
			{
				if (z < fh)
				{
					waterlevel = 1;
					if (z + height/2 < fh)
					{
						waterlevel = 2;
						if ((player && z + player->viewheight <= fh) ||
							(z + height <= fh))
						{
							waterlevel = 3;
						}
					}
				}
				else if (z + height > hsec->ceilingplane.ZatPoint (x, y))
				{
					waterlevel = 3;
				}
			}
			else
			{
				return (oldz >= fh) && (z < fh);
			}
		}
	}

	return (lastwaterlevel == 0 && waterlevel != 0);
}

//----------------------------------------------------------------------------
//
// PROC A_FreeTargMobj
//
//----------------------------------------------------------------------------

void A_FreeTargMobj (AActor *mo)
{
	mo->momx = mo->momy = mo->momz = 0;
	mo->z = mo->ceilingz + 4*FRACUNIT;
	mo->flags &= ~(MF_SHOOTABLE|MF_FLOAT|MF_SKULLFLY|MF_SOLID);
	mo->flags |= MF_CORPSE|MF_DROPOFF|MF_NOGRAVITY;
	mo->flags2 &= ~(MF2_PASSMOBJ|MF2_LOGRAV);
	mo->player = NULL;
}

//==========================================================================
//
// A_GenericFreezeDeath
//
//==========================================================================

void A_GenericFreezeDeath (AActor *actor)
{
	actor->Translation = TRANSLATION(TRANSLATION_Standard, 7);
	A_FreezeDeath (actor);
}

//==========================================================================
//
// AActor stuff
//
//==========================================================================

FState AActor::States[] =
{
	S_NORMAL (TNT1, 'A', -1, NULL, NULL),
	S_NORMAL (TNT1, 'E', 1050, A_FreeTargMobj, NULL),
	S_NORMAL (TNT1, 'A', 1, NULL, NULL),	// S_NULL

	// Generic freeze death frames. Woo!
	S_NORMAL (----, 'A', 5, A_GenericFreezeDeath, &States[4]),
	S_NORMAL (----, 'A', 1, A_FreezeDeathChunks, &States[4])
};

BEGIN_DEFAULTS (AActor, Any, -1, 0)
	PROP_XScale (63)
	PROP_YScale (63)
	PROP_SpawnState (2)
	PROP_SpawnHealth (1000)
	PROP_ReactionTime (8)
	PROP_RadiusFixed (20)
	PROP_HeightFixed (16)
	PROP_Mass (100)
	PROP_RenderStyle (STYLE_Normal)
	PROP_Alpha (FRACUNIT)
	PROP_MinMissileChance (200)
END_DEFAULTS

//==========================================================================
//
// P_SpawnMobj
//
//==========================================================================

AActor *AActor::StaticSpawn (const TypeInfo *type, fixed_t ix, fixed_t iy, fixed_t iz)
{
	if (type == NULL)
	{
		I_Error ("Tried to spawn a class-less actor\n");
	}

	if (type->ActorInfo == NULL)
	{
		I_Error ("%s is not an actor\n", type->Name);
	}

	AActor *actor;

	actor = static_cast<AActor *>(const_cast<TypeInfo *>(type)->CreateNew ());

	actor->x = actor->PrevX = ix;
	actor->y = actor->PrevY = iy;
	actor->z = actor->PrevZ = iz;
	actor->picnum = 0xffff;

	FRandom &rng = bglobal.m_Thinking ? pr_botspawnmobj : pr_spawnmobj;

	if (gameskill == sk_nightmare && actor->flags3 & MF3_ISMONSTER)
		actor->reactiontime = 0;

	if (actor->flags3 & MF3_ISMONSTER)
	{
		actor->LastLook.PlayerNumber = rng() % MAXPLAYERS;
		actor->TIDtoHate = 0;
	}

	// Set the state, but do not use SetState, because action
	// routines can't be called yet.  If the spawnstate has an action
	// routine, it will not be called.
	FState *st = actor->SpawnState;
	actor->state = st;
	actor->tics = st->GetTics();
	actor->sprite = st->sprite.index;
	actor->frame = st->GetFrame();
	actor->renderflags = (actor->renderflags & ~RF_FULLBRIGHT) | st->GetFullbright();
	actor->touching_sectorlist = NULL;	// NULL head of sector list // phares 3/13/98

	// set subsector and/or block links
	actor->LinkToWorld (SpawningMapThing);
	if (SpawningMapThing || !type->IsDescendantOf (RUNTIME_CLASS(APlayerPawn)))
	{
		actor->dropoffz =			// killough 11/98: for tracking dropoffs
		actor->floorz = actor->Sector->floorplane.ZatPoint (ix, iy);
		actor->ceilingz = actor->Sector->ceilingplane.ZatPoint (ix, iy);
		actor->floorsector = actor->Sector;
		actor->floorpic = actor->floorsector->floorpic;
	}
	else
	{
		P_FindFloorCeiling (actor);
		actor->floorz = tmffloorz;
		actor->dropoffz = tmfdropoffz;
		actor->ceilingz = tmfceilingz;
		actor->floorpic = tmffloorpic;
		actor->floorsector = tmffloorsector;
	}

	actor->SpawnPoint[0] = ix >> FRACBITS;
	actor->SpawnPoint[1] = iy >> FRACBITS;

	if (iz == ONFLOORZ)
	{
		actor->z = actor->floorz;
	}
	else if (iz == ONCEILINGZ)
	{
		actor->z = actor->ceilingz - actor->height;
	}
	else if (iz == FLOATRANDZ)
	{
		fixed_t space = actor->ceilingz - actor->height - actor->floorz;
		if (space > 48*FRACUNIT)
		{
			space -= 40*FRACUNIT;
			actor->z = MulScale8 (space, rng()) + actor->floorz + 40*FRACUNIT;
		}
		else
		{
			actor->z = actor->floorz;
		}
	}
	else
	{
		actor->SpawnPoint[2] = (actor->z - actor->floorz) >> FRACBITS;
	}

	if (actor->flags2 & MF2_FLOATBOB)
	{ // Prime the bobber
		actor->FloatBobPhase = rng();
		actor->z += FloatBobOffsets[(actor->FloatBobPhase + level.time - 1) & 63];
	}
	if (actor->flags2 & MF2_FLOORCLIP)
	{
		actor->AdjustFloorClip ();
	}
	else
	{
		actor->floorclip = 0;
	}
	actor->UpdateWaterLevel (actor->z);
	if (!SpawningMapThing)
	{
		actor->BeginPlay ();
		if (actor->ObjectFlags & OF_MassDestruction)
		{
			return NULL;
		}
	}
	// [RH] Count monsters whenever they are spawned.
	if (actor->flags & MF_COUNTKILL)
	{
		level.total_monsters++;
	}
	// [RH] Same, for items
	if (actor->flags & MF_COUNTITEM)
	{
		level.total_items++;
	}
	return actor;
}

void AActor::LevelSpawned ()
{
	if (tics > 0 && !(flags4 & MF4_SYNCHRONIZED))
		tics = 1 + (pr_spawnmapthing() % tics);
	angle_t incs = AngleIncrements ();
	angle -= angle % incs;
	flags &= ~MF_DROPPED;		// [RH] clear MF_DROPPED flag
	HandleSpawnFlags ();
}

void AActor::HandleSpawnFlags ()
{
	if (SpawnFlags & MTF_AMBUSH)
	{
		flags |= MF_AMBUSH;
	}
	if (SpawnFlags & MTF_DORMANT)
	{
		Deactivate (NULL);
	}
	if (SpawnFlags & MTF_STANDSTILL)
	{
		flags4 |= MF4_STANDSTILL;
	}
	if (SpawnFlags & MTF_FRIENDLY)
	{
		flags |= MF_FRIENDLY;
	}
	if (SpawnFlags & MTF_SHADOW)
	{
		flags |= MF_SHADOW;
		RenderStyle = STYLE_Translucent;
		alpha = TRANSLUC25;
	}
	else if (SpawnFlags & MTF_ALTSHADOW)
	{
		RenderStyle = STYLE_None;
	}
}

void AActor::BeginPlay ()
{
}

void AActor::Activate (AActor *activator)
{
	if ((flags3 & MF3_ISMONSTER) && (health > 0 || (flags & MF_ICECORPSE)))
	{
		if (flags2 & MF2_DORMANT)
		{
			flags2 &= ~MF2_DORMANT;
			tics = 1;
		}
	}
}

void AActor::Deactivate (AActor *activator)
{
	if ((flags3 & MF3_ISMONSTER) && (health > 0 || (flags & MF_ICECORPSE)))
	{
		if (!(flags2 & MF2_DORMANT))
		{
			flags2 |= MF2_DORMANT;
			tics = -1;
		}
	}
}

//
// P_RemoveMobj
//

void AActor::Destroy ()
{
	// [RH] Destroy any inventory this actor is carrying
	while (Inventory != NULL)
	{
		AInventory *item = Inventory;
		Inventory = item->Inventory;
		item->Destroy ();
	}

	// [RH] Unlink from tid chain
	RemoveFromHash ();

	// unlink from sector and block lists
	UnlinkFromWorld ();
	flags |= MF_NOSECTOR|MF_NOBLOCKMAP;

	// Delete all nodes on the current sector_list			phares 3/16/98
	if (sector_list)
	{
		P_DelSeclist (sector_list);
		sector_list = NULL;
	}

	// stop any playing sound
	S_RelinkSound (this, NULL);

	Super::Destroy ();
}

//===========================================================================
//
// AdjustFloorClip
//
//===========================================================================

void AActor::AdjustFloorClip ()
{
	if (flags3 & MF3_SPECIALFLOORCLIP)
	{
		return;
	}

	fixed_t oldclip = floorclip;
	fixed_t shallowestclip = FIXED_MAX;
	const msecnode_t *m;

	// [RH] clip based on shallowest floor player is standing on
	// If the sector has a deep water effect, then let that effect
	// do the floorclipping instead of the terrain type.
	for (m = touching_sectorlist; m; m = m->m_tnext)
	{
		if ((m->m_sector->heightsec == NULL ||
			 m->m_sector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC) &&
			m->m_sector->floorplane.ZatPoint (x, y) == z)
		{
			fixed_t clip = Terrains[TerrainTypes[m->m_sector->floorpic]].FootClip;
			if (clip < shallowestclip)
			{
				shallowestclip = clip;
			}
		}
	}
	if (shallowestclip == FIXED_MAX)
	{
		floorclip = 0;
	}
	else
	{
		floorclip = shallowestclip;
	}
	if (player && player->mo == this && oldclip != floorclip)
	{
		player->viewheight -= oldclip - floorclip;
		player->deltaviewheight = (VIEWHEIGHT - player->viewheight) >> 3;
	}
}

//
// P_SpawnPlayer
// Called when a player is spawned on the level.
// Most of the player structure stays unchanged between levels.
//
EXTERN_CVAR (Bool, chasedemo)
extern BOOL demonew;

void P_SpawnPlayer (mapthing2_t *mthing)
{
	int		  playernum;
	player_t *p;
	APlayerPawn *mobj, *oldactor;
	int 	  i;
	BYTE	  state;

	// [RH] Things 4001-? are also multiplayer starts. Just like 1-4.
	//		To make things simpler, figure out which player is being
	//		spawned here.
	if (mthing->type <= 4)
	{
		playernum = mthing->type - 1;
	}
	else if (gameinfo.gametype != GAME_Hexen)
	{
		playernum = mthing->type - 4001 + 4;
	}
	else
	{
		playernum = mthing->type - 9100 + 4;
	}

	// not playing?
	if (playernum >= MAXPLAYERS || !playeringame[playernum])
		return;

	p = &players[playernum];

	if (p->cls == NULL)
	{
		p->CurrentPlayerClass = 0;
		if (gameinfo.gametype == GAME_Doom)
		{
			p->cls = TypeInfo::FindType ("DoomPlayer");
		}
		else if (gameinfo.gametype == GAME_Heretic)
		{
			p->cls = TypeInfo::FindType ("HereticPlayer");
		}
		else if (gameinfo.gametype == GAME_Strife)
		{
			p->cls = TypeInfo::FindType ("StrifePlayer");
		}
		else
		{
			static const char *classes[3] = { "FighterPlayer", "ClericPlayer", "MagePlayer" };
			int type;

			if (!deathmatch || !multiplayer)
			{
				type = SinglePlayerClass[playernum];
			}
			else
			{
				type = p->userinfo.PlayerClass;
				if (type < 0)
				{
					type = pr_multiclasschoice() % 3;
				}
			}
			p->CurrentPlayerClass = type;
			p->cls = TypeInfo::FindType (classes[type]);
		}
	}

	mobj = static_cast<APlayerPawn *>
		(Spawn (p->cls, mthing->x << FRACBITS, mthing->y << FRACBITS, ONFLOORZ));

	oldactor = p->mo;
	p->mo = mobj;
	mobj->player = p;
	state = p->playerstate;
	if (state == PST_REBORN || state == PST_ENTER)
	{
		G_PlayerReborn (playernum);
	}
	else if (oldactor != NULL && oldactor->player == p)
	{
		// Move the voodoo doll's inventory to the new player.
		mobj->ObtainInventory (oldactor);
	}

	// [RH] Be sure the player has the right translation
	R_BuildPlayerTranslation (playernum);

	// [RH] set color translations for player sprites
	mobj->Translation = TRANSLATION(TRANSLATION_Players,playernum);

	mobj->angle = ANG45 * (mthing->angle/45);
	mobj->pitch = mobj->roll = 0;
	mobj->health = p->health;

	//Added by MC: Identification (number in the players[MAXPLAYERS] array)
    mobj->id = playernum;

	// [RH] Set player sprite based on skin
	p->skin = &skins[p->userinfo.skin];
	if (gameinfo.gametype != GAME_Hexen)
	{
		mobj->sprite = p->skin->sprite;
		mobj->xscale = mobj->yscale = p->skin->scale;
	}

	p->DesiredFOV = p->FOV = 90.f;
	p->camera = p->mo;
	p->playerstate = PST_LIVE;
	p->refire = 0;
	p->damagecount = 0;
	p->bonuscount = 0;
	p->morphTics = 0;
	p->extralight = 0;
	p->fixedcolormap = 0;
	p->viewheight = VIEWHEIGHT;
	p->inconsistant = 0;
	p->attacker = NULL;
	p->spreecount = 0;
	p->multicount = 0;
	p->lastkilltime = 0;
	p->BlendR = p->BlendG = p->BlendB = p->BlendA = 0.f;
	p->air_finished = level.time + 10*TICRATE;

	p->momx = p->momy = 0;		// killough 10/98: initialize bobbing to 0.

	if (players[consoleplayer].camera == oldactor)
	{
		players[consoleplayer].camera = mobj;
	}

	// [RH] Allow chasecam for demo watching
	if ((demoplayback || demonew) && chasedemo)
		p->cheats = CF_CHASECAM;

	// setup gun psprite
	P_SetupPsprites (p);

	// give all cards in death match mode
	if (deathmatch)
	{
		for (i = 0; i < TypeInfo::m_NumTypes; ++i)
		{
			if (TypeInfo::m_Types[i]->IsDescendantOf (RUNTIME_CLASS(AKey)))
			{
				AKey *key = (AKey *)GetDefaultByType (TypeInfo::m_Types[i]);
				if (key->KeyNumber != 0)
				{
					key = static_cast<AKey *>(Spawn (TypeInfo::m_Types[i], 0,0,0));
					if (!key->TryPickup (p->mo))
					{
						key->Destroy ();
					}
				}
			}
		}
	}

	if (StatusBar != NULL && StatusBar->GetPlayer() == playernum)
	{
		StatusBar->AttachToPlayer (p);
	}

	if (multiplayer)
	{
		unsigned an = ( ANG45 * (mthing->angle/45) ) >> ANGLETOFINESHIFT;
		Spawn ("TeleportFog", mobj->x+20*finecosine[an], mobj->y+20*finesine[an], mobj->z + TELEFOGHEIGHT);
	}

	// "Fix" for one of the starts on exec.wad MAP01: If you start inside the ceiling,
	// drop down below it, even if that means sinking into the floor.
	if (mobj->z + mobj->height > mobj->ceilingz)
	{
		mobj->z = mobj->ceilingz - mobj->height;
	}

	// [RH] If someone is in the way, kill them
	P_PlayerStartStomp (mobj);

	// [BC] Do script stuff
	if (level.behavior != NULL)
	{
		if (state == PST_ENTER || (state == PST_LIVE && !savegamerestore))
		{
			FBehavior::StaticStartTypedScripts (SCRIPT_Enter, p->mo, true);
		}
		else if (state == PST_REBORN)
		{
			FBehavior::StaticStartTypedScripts (SCRIPT_Respawn, p->mo, true);
		}
	}
}


//
// P_SpawnMapThing
// The fields of the mapthing should
// already be in host byte order.
//
// [RH] position is used to weed out unwanted start spots
void P_SpawnMapThing (mapthing2_t *mthing, int position)
{
	const TypeInfo *i;
	int mask;
	AActor *mobj;
	fixed_t x, y, z;
	static unsigned int classFlags[] =
	{
		MTF_FIGHTER,
		MTF_CLERIC,
		MTF_MAGE
	};

	if (mthing->type == 0 || mthing->type == -1)
		return;

	// count deathmatch start positions
	if (mthing->type == 11)
	{
		deathmatchstarts.Push (*mthing);
		return;
	}

	// Convert Strife starts to Hexen-style starts
	if (gameinfo.gametype == GAME_Strife && mthing->type >= 118 && mthing->type <= 127)
	{
		mthing->args[0] = mthing->type - 117;
		mthing->type = 1;
	}

	// [RH] Record polyobject-related things
	if (gameinfo.gametype == GAME_Hexen)
	{
		switch (mthing->type)
		{
		case PO_HEX_ANCHOR_TYPE:
			mthing->type = PO_ANCHOR_TYPE;
			break;
		case PO_HEX_SPAWN_TYPE:
			mthing->type = PO_SPAWN_TYPE;
			break;
		case PO_HEX_SPAWNCRUSH_TYPE:
			mthing->type = PO_SPAWNCRUSH_TYPE;
			break;
		}
	}

	if (mthing->type == PO_ANCHOR_TYPE ||
		mthing->type == PO_SPAWN_TYPE ||
		mthing->type == PO_SPAWNCRUSH_TYPE ||
		mthing->type == PO_SPAWNHURT_TYPE)
	{
		polyspawns_t *polyspawn = new polyspawns_t;
		polyspawn->next = polyspawns;
		polyspawn->x = mthing->x << FRACBITS;
		polyspawn->y = mthing->y << FRACBITS;
		polyspawn->angle = mthing->angle;
		polyspawn->type = mthing->type;
		polyspawns = polyspawn;
		if (mthing->type != PO_ANCHOR_TYPE)
			po_NumPolyobjs++;
		return;
	}

	// check for players specially
	int pnum = -1;

	if (mthing->type <= 4 && mthing->type > 0)
	{
		pnum = mthing->type - 1;
	}
	else
	{
		const int base = (gameinfo.gametype == GAME_Strife) ? 5 :
						 (gameinfo.gametype == GAME_Hexen) ? 9100 : 4001;

		if (mthing->type >= base && mthing->type < base + MAXPLAYERS - 4)
		{
			pnum = mthing->type - base + 4;
		}
	}

	if (pnum == -1 || (level.flags & LEVEL_FILTERSTARTS))
	{
		// check for appropriate game type
		if (deathmatch) 
		{
			mask = MTF_DEATHMATCH;
		}
		else if (multiplayer)
		{
			mask = MTF_COOPERATIVE;
		}
		else
		{
			mask = MTF_SINGLE;
		}
		if (!(mthing->flags & mask))
		{
			return;
		}

		// check for apropriate skill level
		if (gameskill == sk_baby)
		{
			mask = MTF_EASY;
		}
		else if (gameskill == sk_nightmare)
		{
			mask = MTF_HARD;
		}
		else
		{
			mask = 1 << (gameskill - 1);
		}
		if (!(mthing->flags & mask))
		{
			return;
		}

		// Check current character classes with spawn flags
		if (gameinfo.gametype == GAME_Hexen)
		{
			if (!multiplayer)
			{ // Single player
				if ((mthing->flags & classFlags[players[consoleplayer].CurrentPlayerClass]) == 0)
				{ // Not for current class
					return;
				}
			}
			else if (!deathmatch)
			{ // Cooperative
				mask = 0;
				for (int i = 0; i < MAXPLAYERS; i++)
				{
					if (playeringame[i])
					{
						mask |= classFlags[players[i].CurrentPlayerClass];
					}
				}
				if ((mthing->flags & mask) == 0)
				{
					return;
				}
			}
		}
	}

	if (pnum != -1)
	{
		// [RH] Only spawn spots that match position.
		if (mthing->args[0] != position)
			return;

		// save spots for respawning in network games
		playerstarts[pnum] = *mthing;
		if (!deathmatch)
			P_SpawnPlayer (mthing);

		return;
	}

	// [RH] sound sequence overriders
	if (mthing->type >= 1400 && mthing->type < 1410)
	{
		R_PointInSubsector (mthing->x<<FRACBITS,
			mthing->y<<FRACBITS)->sector->seqType = mthing->type - 1400;
		return;
	}
	else if (mthing->type == 1411)
	{
		int type;

		if (mthing->args[0] == 255)
			type = -1;
		else
			type = mthing->args[0];

		if (type > 63)
		{
			Printf ("Sound sequence %d out of range\n", type);
		}
		else
		{
			R_PointInSubsector (mthing->x << FRACBITS,
				mthing->y << FRACBITS)->sector->seqType = type;
		}
		return;
	}

	// [RH] Determine if it is an old ambient thing, and if so,
	//		map it to MT_AMBIENT with the proper parameter.
	if (mthing->type >= 14001 && mthing->type <= 14064)
	{
		mthing->args[0] = mthing->type - 14000;
		mthing->type = 14065;
		i = RUNTIME_CLASS(AAmbientSound);
	}
	else
	{
		// find which type to spawn
		i = DoomEdMap.FindType (mthing->type);
	}

	if (i == NULL)
	{
		// [RH] Don't die if the map tries to spawn an unknown thing
		Printf ("Unknown type %i at (%i, %i)\n",
				 mthing->type,
				 mthing->x, mthing->y);
		i = RUNTIME_CLASS(AUnknown);
	}
	// [RH] If the thing's corresponding sprite has no frames, also map
	//		it to the unknown thing.
	else
	{
		const AActor *defaults = GetDefaultByType (i);
		if (defaults->SpawnState == NULL ||
			sprites[defaults->SpawnState->sprite.index].numframes == 0)
		{
			Printf ("%s at (%i, %i) has no frames\n",
					i->Name+1, mthing->x, mthing->y);
			i = RUNTIME_CLASS(AUnknown);
		}
	}

	const AActor *info = GetDefaultByType (i);

	// don't spawn keycards and players in deathmatch
	if (deathmatch && info->flags & MF_NOTDMATCH)
		return;

	// [RH] don't spawn extra weapons in coop
	if (multiplayer && !deathmatch)
	{
		if (i->IsDescendantOf (RUNTIME_CLASS(AWeapon)))
		{
			if ((mthing->flags & (MTF_DEATHMATCH|MTF_SINGLE)) == MTF_DEATHMATCH)
				return;
		}
	}

	// don't spawn any monsters if -nomonsters
	if (dmflags & DF_NO_MONSTERS && info->flags3 & MF3_ISMONSTER )
	{
		return;
	}
	
	// [RH] Other things that shouldn't be spawned depending on dmflags
	if (deathmatch || alwaysapplydmflags)
	{
		if (dmflags & DF_NO_HEALTH)
		{
			if (i->IsDescendantOf (RUNTIME_CLASS(AHealth)))
				return;
			if (strcmp (i->Name, "Berserk") == 0)
				return;
			if (strcmp (i->Name, "Soulsphere") == 0)
				return;
			if (strcmp (i->Name, "Megasphere") == 0)
				return;
		}
		if (dmflags & DF_NO_ITEMS)
		{
//			if (i->IsDescendantOf (RUNTIME_CLASS(AArtifact)))
//				return;
		}
		if (dmflags & DF_NO_ARMOR)
		{
			if (i->IsDescendantOf (RUNTIME_CLASS(AArmor)))
				return;
			if (strcmp (i->Name, "Megasphere") == 0)
				return;
		}
	}

	// spawn it
	x = mthing->x << FRACBITS;
	y = mthing->y << FRACBITS;

	if (info->flags & MF_SPAWNCEILING)
		z = ONCEILINGZ;
	else if (info->flags2 & MF2_SPAWNFLOAT)
		z = FLOATRANDZ;
	else
		z = ONFLOORZ;

	SpawningMapThing = true;
	mobj = Spawn (i, x, y, z);
	SpawningMapThing = false;

	if (z == ONFLOORZ)
		mobj->z += mthing->z << FRACBITS;
	else if (z == ONCEILINGZ)
		mobj->z -= mthing->z << FRACBITS;

	mobj->SpawnPoint[0] = mthing->x;
	mobj->SpawnPoint[1] = mthing->y;
	mobj->SpawnPoint[2] = mthing->z;
	mobj->SpawnAngle = mthing->angle;
	mobj->SpawnFlags = mthing->flags;

	// [RH] Set the thing's special
	mobj->special = mthing->special;
	memcpy (mobj->args, mthing->args, sizeof(mobj->args));

	// [RH] Add ThingID to mobj and link it in with the others
	mobj->tid = mthing->thingid;
	mobj->AddToHash ();

	mobj->angle = (DWORD)((mthing->angle * UCONST64(0x100000000)) / 360);
	mobj->BeginPlay ();
	if (mobj->ObjectFlags & OF_MassDestruction)
	{
		return;
	}
	mobj->LevelSpawned ();
}



//
// GAME SPAWN FUNCTIONS
//


//
// P_SpawnPuff
//

AActor *P_SpawnPuff (const TypeInfo *pufftype, fixed_t x, fixed_t y, fixed_t z, angle_t dir, int updown, bool hitthing)
{
	AActor *puff;

	z += pr_spawnpuff.Random2 () << 10;

	puff = Spawn (pufftype, x, y, z);

	// If a puff has a crash state and an actor was not hit,
	// it will enter the crash state. This is used by the StrifeSpark
	// and BlasterPuff.
	if (hitthing == false && puff->CrashState != NULL)
	{
		puff->SetState (puff->CrashState);
	}

	if (gameinfo.gametype == GAME_Doom)
	{
		// don't make punches spark on the wall
		if (attackrange == MELEERANGE)
		{
			FState *state = puff->state;
			int i;

			for (i = 0; i < 2 && state->GetNextState(); i++)
				state = state->GetNextState();

			puff->SetState (state);
		}
		if (cl_pufftype && updown != 3)
		{
			P_DrawSplash2 (32, x, y, z, dir, updown, 1);
			puff->renderflags |= RF_INVISIBLE;
		}
	}

	if (hitthing && puff->SeeSound)
	{ // Hit thing sound
		S_SoundID (puff, CHAN_BODY, puff->SeeSound, 1, ATTN_NORM);
	}
	else if (puff->AttackSound)
	{
		S_SoundID (puff, CHAN_BODY, puff->AttackSound, 1, ATTN_NORM);
	}

	PuffSpawned = puff;
	return puff;
}



//
// P_SpawnBlood
// 
void P_SpawnBlood (fixed_t x, fixed_t y, fixed_t z, angle_t dir, int damage)
{
	ABlood *th;

	if (cl_bloodtype <= 1)
	{
		z += pr_spawnblood.Random2 () << 10;
		th = Spawn<ABlood> (x, y, z);
		th->momz = FRACUNIT*2;
		if (gameinfo.gametype == GAME_Doom)
		{
			th->tics -= pr_spawnblood() & 3;

			if (th->tics < 1)
				th->tics = 1;
		}
		th->SetDamage (damage);
	}

	if (cl_bloodtype >= 1)
		P_DrawSplash2 (40, x, y, z, dir, 2, 0);
}

// Blood splatter -----------------------------------------------------------

class ABloodSplatter : public AActor
{
	DECLARE_ACTOR (ABloodSplatter, AActor)
};

FState ABloodSplatter::States[] =
{
#define S_BLOODSPLATTER 0
	S_NORMAL (BLOD, 'C',	8, NULL, &States[S_BLOODSPLATTER+1]),
	S_NORMAL (BLOD, 'B',	8, NULL, &States[S_BLOODSPLATTER+2]),
	S_NORMAL (BLOD, 'A',	8, NULL, NULL),

#define S_BLOODSPLATTERX (S_BLOODSPLATTER+3)
	S_NORMAL (BLOD, 'A',	6, NULL, NULL)
};

IMPLEMENT_ACTOR (ABloodSplatter, Raven, -1, 0)
	PROP_SpawnState (S_BLOODSPLATTER)
	PROP_DeathState (S_BLOODSPLATTERX)
	PROP_RadiusFixed (2)
	PROP_HeightFixed (4)
	PROP_Flags (MF_NOBLOCKMAP|MF_MISSILE|MF_DROPOFF)
	PROP_Flags2 (MF2_NOTELEPORT|MF2_CANNOTPUSH)
	PROP_Mass (5)
END_DEFAULTS

//---------------------------------------------------------------------------
//
// PROC P_BloodSplatter
//
//---------------------------------------------------------------------------

void P_BloodSplatter (fixed_t x, fixed_t y, fixed_t z, AActor *originator)
{
	if (cl_bloodtype <= 1)
	{
		AActor *mo;

		if (gameinfo.gametype == GAME_Doom)
		{
			mo = Spawn<ABlood> (x, y, z);
		}
		else
		{
			mo = Spawn<ABloodSplatter> (x, y, z);
		}
		mo->target = originator;
		mo->momx = pr_splatter.Random2 () << 10;
		mo->momy = pr_splatter.Random2 () << 10;
		mo->momz = 3*FRACUNIT;
	}
	if (cl_bloodtype >= 1)
	{
		P_DrawSplash2 (40, x, y, z, R_PointToAngle (originator->x - x, originator->y - y), 2, 0);
	}
}

//---------------------------------------------------------------------------
//
// PROC P_RipperBlood
//
//---------------------------------------------------------------------------

void P_RipperBlood (AActor *mo)
{
	fixed_t x, y, z;

	x = mo->x + (pr_ripperblood.Random2 () << 12);
	y = mo->y + (pr_ripperblood.Random2 () << 12);
	z = mo->z + (pr_ripperblood.Random2 () << 12);
	if (cl_bloodtype <= 1)
	{
		AActor *th;
		th = Spawn<ABlood> (x, y, z);
		if (gameinfo.gametype == GAME_Heretic)
			th->flags |= MF_NOGRAVITY;
		th->momx = mo->momx >> 1;
		th->momy = mo->momy >> 1;
		th->tics += pr_ripperblood () & 3;
	}
	if (cl_bloodtype >= 1)
	{
		P_DrawSplash2 (28, x, y, z, 0, 0, 0);
	}
}

//---------------------------------------------------------------------------
//
// FUNC P_GetThingFloorType
//
//---------------------------------------------------------------------------

int P_GetThingFloorType (AActor *thing)
{
	if (thing->floorpic)
	{		
		return TerrainTypes[thing->floorpic];
	}
	else
	{
		return TerrainTypes[thing->Sector->floorpic];
	}
}

//---------------------------------------------------------------------------
//
// FUNC P_HitWater
//
// Returns true if hit liquid and splashed, false if not.
//---------------------------------------------------------------------------

bool P_HitWater (AActor *thing, sector_t *sec)
{
	if (thing->flags2 & MF2_FLOATBOB || thing->flags3 & MF3_DONTSPLASH)
		return false;

	if (thing->player && (thing->player->cheats & CF_PREDICTING))
		return false;

	AActor *mo = NULL;
	FSplashDef *splash;
	int terrainnum;
	
	if (sec->heightsec == NULL ||
		//!sec->heightsec->waterzone ||
		(sec->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC) ||
		!(sec->heightsec->MoreFlags & SECF_CLIPFAKEPLANES))
	{
		terrainnum = TerrainTypes[sec->floorpic];
	}
	else
	{
		terrainnum = TerrainTypes[sec->heightsec->floorpic];
	}

	int splashnum = Terrains[terrainnum].Splash;
	bool smallsplash = false;
	const secplane_t *plane;
	fixed_t z;

	if (splashnum == -1)
		return Terrains[terrainnum].IsLiquid;

	plane = (sec->heightsec != NULL &&
		//sec->heightsec->waterzone &&
		!(sec->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC))
	  ? &sec->heightsec->floorplane : &sec->floorplane;
	z = plane->ZatPoint (thing->x, thing->y);

	splash = &Splashes[splashnum];

	// Small splash for small masses
	if (thing->Mass < 10)
		smallsplash = true;

	if (smallsplash && splash->SmallSplash)
	{
		mo = Spawn (splash->SmallSplash, thing->x, thing->y, z);
		if (mo) mo->floorclip += splash->SmallSplashClip;
	}
	else
	{
		if (splash->SplashChunk)
		{
			mo = Spawn (splash->SplashChunk, thing->x, thing->y, z);
			mo->target = thing;
			if (splash->ChunkXVelShift != 255)
			{
				mo->momx = pr_chunk.Random2() << splash->ChunkXVelShift;
			}
			if (splash->ChunkYVelShift != 255)
			{
				mo->momy = pr_chunk.Random2() << splash->ChunkYVelShift;
			}
			mo->momz = splash->ChunkBaseZVel + (pr_chunk() << splash->ChunkZVelShift);
		}
		if (splash->SplashBase)
		{
			mo = Spawn (splash->SplashBase, thing->x, thing->y, z);
		}
		if (thing->player && !splash->NoAlert)
		{
			P_NoiseAlert (thing, thing, true);
		}
	}
	if (mo)
	{
		S_SoundID (mo, CHAN_ITEM, smallsplash ?
			splash->SmallSplashSound : splash->NormalSplashSound,
			1, ATTN_IDLE);
	}
	else
	{
		S_SoundID (thing->x, thing->y, z, CHAN_ITEM, smallsplash ?
			splash->SmallSplashSound : splash->NormalSplashSound,
			1, ATTN_IDLE);
	}

	// Don't let deep water eat missiles
	return plane == &sec->floorplane ? Terrains[terrainnum].IsLiquid : false;
}

//---------------------------------------------------------------------------
//
// FUNC P_HitFloor
//
// Returns true if hit liquid and splashed, false if not.
//---------------------------------------------------------------------------

bool P_HitFloor (AActor *thing)
{
	const msecnode_t *m;

	if (thing->flags2 & MF2_FLOATBOB || thing->flags3 & MF3_DONTSPLASH)
		return false;

	// don't splash if landing on the edge above water/lava/etc....
	for (m = thing->touching_sectorlist; m; m = m->m_tnext)
	{
		if (thing->z == m->m_sector->floorplane.ZatPoint (thing->x, thing->y))
		{
			break;
		}
	}
	if (m == NULL ||
		(m->m_sector->heightsec != NULL &&
		!(m->m_sector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC)))
	{ 
		return false;
	}

	return P_HitWater (thing, m->m_sector);
}

//---------------------------------------------------------------------------
//
// FUNC P_CheckMissileSpawn
//
// Returns true if the missile is at a valid spawn point, otherwise
// explodes it and returns false.
//
//---------------------------------------------------------------------------

bool P_CheckMissileSpawn (AActor* th)
{
	// [RH] Don't decrement tics if they are already less than 1
	if ((th->flags4 & MF4_RANDOMIZE) && th->tics > 0)
	{
		th->tics -= pr_checkmissilespawn() & 3;
		if (th->tics < 1)
			th->tics = 1;
	}

	// move a little forward so an angle can be computed if it immediately explodes
	if (th->Speed >= 100*FRACUNIT)
	{ // Ultra-fast ripper spawning missile
		th->x += th->momx>>3;
		th->y += th->momy>>3;
		th->z += th->momz>>3;
	}
	else
	{ // Normal missile
		th->x += th->momx>>1;
		th->y += th->momy>>1;
		th->z += th->momz>>1;
	}

	// killough 3/15/98: no dropoff (really = don't care for missiles)

	if (!P_TryMove (th, th->x, th->y, false))
	{
		// [RH] Don't explode ripping missiles that spawn inside something
		if (BlockingMobj == NULL || !(th->flags2 & MF2_RIP))
		{
			// [RH] Don't explode missiles that spawn on top of horizon lines
			if (BlockingLine != NULL && BlockingLine->special == Line_Horizon)
			{
				th->Destroy ();
			}
			else
			{
				P_ExplodeMissile (th, NULL);
			}
			return false;
		}
	}
	return true;
}


//---------------------------------------------------------------------------
//
// FUNC P_SpawnMissile
//
// Returns NULL if the missile exploded immediately, otherwise returns
// a mobj_t pointer to the missile.
//
//---------------------------------------------------------------------------

AActor *P_SpawnMissile (AActor *source, AActor *dest, const TypeInfo *type)
{
	return P_SpawnMissileXYZ (source->x, source->y, source->z + 32*FRACUNIT,
		source, dest, type);
}

AActor *P_SpawnMissileZ (AActor *source, fixed_t z, AActor *dest, const TypeInfo *type)
{
	return P_SpawnMissileXYZ (source->x, source->y, z, source, dest, type);
}

AActor *P_SpawnMissileXYZ (fixed_t x, fixed_t y, fixed_t z,
	AActor *source, AActor *dest, const TypeInfo *type)
{
	int defflags3 = GetDefaultByType (type)->flags3;

	if (defflags3 & MF3_FLOORHUGGER)
	{
		z = ONFLOORZ;
	}
	else if (defflags3 & MF3_CEILINGHUGGER)
	{
		z = ONCEILINGZ;
	}
	else if (z != ONFLOORZ)
	{
		z -= source->floorclip;
	}

	AActor *th = Spawn (type, x, y, z);
	
	if (th->SeeSound)
		S_SoundID (th, CHAN_VOICE, th->SeeSound, 1, ATTN_NORM);

	th->target = source;		// record missile's originator

	vec3_t velocity;
	float speed = (float)(th->Speed);

	// [RH]
	// Hexen calculates the missile velocity based on the source's location.
	// Would it be more useful to base it on the actual position of the
	// missile? I'll leave it like this for now.
	// Answer. No, because this way, you can set up sets of parallel missiles.

	velocity[0] = (float)(dest->x - source->x);
	velocity[1] = (float)(dest->y - source->y);
	velocity[2] = (float)(dest->z - source->z);
	// Floor and ceiling huggers should never have a vertical component to their velocity
	if (defflags3 & (MF3_FLOORHUGGER|MF3_CEILINGHUGGER))
	{
		velocity[2] = 0.f;
	}
	// [RH] Adjust the trajectory if the missile will go over the player's head.
	else if (z - source->z >= dest->height)
	{
		velocity[2] += (float)(dest->height - z + source->z);
	}
	VectorNormalize (velocity);
	th->momx = (fixed_t)(velocity[0] * speed);
	th->momy = (fixed_t)(velocity[1] * speed);
	th->momz = (fixed_t)(velocity[2] * speed);

	// invisible target: rotate velocity vector in 2D
	if (dest->flags & MF_SHADOW)
	{
		angle_t an = pr_spawnmissile.Random2 () << 20;
		an >>= ANGLETOFINESHIFT;
		
		fixed_t newx = DMulScale16 (th->momx, finecosine[an], -th->momy, finesine[an]);
		fixed_t newy = DMulScale16 (th->momx, finesine[an], th->momy, finecosine[an]);
		th->momx = newx;
		th->momy = newy;
	}

	th->angle = R_PointToAngle2 (0, 0, th->momx, th->momy);

	return P_CheckMissileSpawn (th) ? th : NULL;
}

//---------------------------------------------------------------------------
//
// FUNC P_SpawnMissileAngle
//
// Returns NULL if the missile exploded immediately, otherwise returns
// a mobj_t pointer to the missile.
//
//---------------------------------------------------------------------------

AActor *P_SpawnMissileAngle (AActor *source, const TypeInfo *type,
	angle_t angle, fixed_t momz)
{
	return P_SpawnMissileAngleZSpeed (source, source->z + 32*FRACUNIT,
		type, angle, momz, GetDefaultByType (type)->Speed);
}

AActor *P_SpawnMissileAngleZ (AActor *source, fixed_t z,
	const TypeInfo *type, angle_t angle, fixed_t momz)
{
	return P_SpawnMissileAngleZSpeed (source, z, type, angle, momz,
		GetDefaultByType (type)->Speed);
}

AActor *P_SpawnMissileZAimed (AActor *source, fixed_t z, AActor *dest, const TypeInfo *type)
{
	angle_t an;
	fixed_t dist;
	fixed_t speed;
	fixed_t momz;

	an = source->angle;

	if (dest->flags & MF_SHADOW)
	{
		an += pr_spawnmissile.Random2() << 20;
	}
	dist = P_AproxDistance (dest->x - source->x, dest->y - source->y);
	speed = GetDefaultByType (type)->Speed;
	dist /= speed;
	momz = dist != 0 ? (dest->z - source->z)/dist : speed;
	return P_SpawnMissileAngleZSpeed (source, z, type, an, momz, speed);
}

//---------------------------------------------------------------------------
//
// FUNC P_SpawnMissileAngleSpeed
//
// Returns NULL if the missile exploded immediately, otherwise returns
// a mobj_t pointer to the missile.
//
//---------------------------------------------------------------------------

AActor *P_SpawnMissileAngleSpeed (AActor *source, const TypeInfo *type,
	angle_t angle, fixed_t momz, fixed_t speed)
{
	return P_SpawnMissileAngleZSpeed (source, source->z + 32*FRACUNIT,
		type, angle, momz, speed);
}

AActor *P_SpawnMissileAngleZSpeed (AActor *source, fixed_t z,
	const TypeInfo *type, angle_t angle, fixed_t momz, fixed_t speed, AActor *owner)
{
	AActor *mo;
	int defflags3 = GetDefaultByType (type)->flags3;

	if (defflags3 & MF3_FLOORHUGGER)
	{
		z = ONFLOORZ;
	}
	else if (defflags3 & MF3_CEILINGHUGGER)
	{
		z = ONCEILINGZ;
	}
	if (z != ONFLOORZ)
	{
		z -= source->floorclip;
	}
	mo = Spawn (type, source->x, source->y, z);
	if (mo->SeeSound)
	{
		S_SoundID (mo, CHAN_VOICE, mo->SeeSound, 1, ATTN_NORM);
	}
	mo->target = owner != NULL ? owner : source; // Originator
	mo->angle = angle;
	angle >>= ANGLETOFINESHIFT;
	mo->momx = FixedMul (speed, finecosine[angle]);
	mo->momy = FixedMul (speed, finesine[angle]);
	mo->momz = momz;
	return P_CheckMissileSpawn(mo) ? mo : NULL;
}

/*
================
=
= P_SpawnPlayerMissile
=
= Tries to aim at a nearby monster
================
*/

AActor *P_SpawnPlayerMissile (AActor *source, const TypeInfo *type)
{
	return P_SpawnPlayerMissile (source, source->x, source->y, source->z, type, source->angle);
}

AActor *P_SpawnPlayerMissile (AActor *source, const TypeInfo *type, angle_t angle)
{
	return P_SpawnPlayerMissile (source, source->x, source->y, source->z, type, angle);
}

AActor *P_SpawnPlayerMissile (AActor *source, fixed_t x, fixed_t y, fixed_t z,
							  const TypeInfo *type, angle_t angle)
{
	static const int angdiff[3] = { -1<<26, 1<<26, 0 };
	int i;
	angle_t an;
	angle_t pitch;

	// see which target is to be aimed at
	i = 2;
	do
	{
		an = angle + angdiff[i];
		pitch = P_AimLineAttack (source, an, 16*64*FRACUNIT);

		if (source->player != NULL &&
			!(dmflags & DF_NO_FREELOOK) &&
			source->player->userinfo.aimdist <= ANGLE_1/2)
		{
			break;
		}
	} while (linetarget == NULL && --i >= 0);

	if (linetarget == NULL)
	{
		an = angle;
	}

	i = GetDefaultByType (type)->flags3;

	if (i & MF3_FLOORHUGGER)
	{
		z = ONFLOORZ;
	}
	else if (i & MF3_CEILINGHUGGER)
	{
		z = ONCEILINGZ;
	}
	if (z != ONFLOORZ && z != ONCEILINGZ)
	{
		z += 4*8*FRACUNIT - source->floorclip;
	}
	MissileActor = Spawn (type, x, y, z);

	if (MissileActor->SeeSound)
	{
		S_SoundID (MissileActor, CHAN_VOICE, MissileActor->SeeSound, 1, ATTN_NORM);
	}
	MissileActor->target = source;
	MissileActor->angle = an;

	fixed_t vx, vy, vz, speed;

	vx = FixedMul (finecosine[pitch>>ANGLETOFINESHIFT], finecosine[an>>ANGLETOFINESHIFT]);
	vy = FixedMul (finecosine[pitch>>ANGLETOFINESHIFT], finesine[an>>ANGLETOFINESHIFT]);
	vz = -finesine[pitch>>ANGLETOFINESHIFT];
	speed = MissileActor->Speed;

	MissileActor->momx = FixedMul (vx, speed);
	MissileActor->momy = FixedMul (vy, speed);
	MissileActor->momz = FixedMul (vz, speed);

	if (P_CheckMissileSpawn (MissileActor))
	{
		return MissileActor;
	}
	return NULL;
}

bool AActor::IsTeammate (AActor *other)
{
	if (!player || !other || !other->player)
		return false;
	if (!deathmatch)
		return true;
	if (teamplay && other->player->userinfo.team != TEAM_None &&
		player->userinfo.team == other->player->userinfo.team)
	{
		return true;
	}
	return false;
}

int AActor::DoSpecialDamage (AActor *target, int damage)
{
	if (target->player && target->player->mo == target && damage < 1000 &&
		(target->player->cheats & CF_GODMODE))
	{
		return -1;
	}
	else
	{
		return damage;
	}
}

int AActor::TakeSpecialDamage (AActor *inflictor, AActor *source, int damage, int damagetype)
{
	// If the actor does not have a corresponding death state, then it does not take damage.
	// Note that DeathState matches every kind of damagetype, so an actor has that, it can
	// be hurt with any type of damage. Exception: Massacre damage always succeeds, because
	// it needs to work.
	FState *death;

	if (DeathState != NULL)
	{
		return damage;
	}

	switch (damagetype)
	{
	case MOD_MASSACRE:
		return damage;

	case MOD_DISINTEGRATE:
		death = EDeathState;
		break;

	case MOD_FIRE:
		death = BDeathState;
		break;

	case MOD_ICE:
		death = IDeathState;
		if (death == NULL && !deh.NoAutofreeze && !(flags4 & MF4_NOICEDEATH) &&
			(player || (flags3 & MF3_ISMONSTER)))
		{
			death = &AActor::States[S_GENERICFREEZEDEATH];
		}
		break;

	default:
		death = NULL;
		break;
	}
	return (death == NULL) ? -1 : damage;
}

FArchive &operator<< (FArchive &arc, FSoundIndex &snd)
{
	if (arc.IsStoring ())
	{
		arc.WriteName (snd.Index ? S_sfx[snd.Index].name : NULL);
	}
	else
	{
		const char *name = arc.ReadName ();;
		snd.Index = name != NULL ? S_FindSound (name) : 0;
	}
	return arc;
}

FArchive &operator<< (FArchive &arc, FSoundIndexWord &snd)
{
	FSoundIndex snd2 = { snd.Index };
	arc << snd2;
	snd.Index = snd2.Index;
	return arc;
}
