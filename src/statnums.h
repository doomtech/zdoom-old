/*
** statnums.h
**
**---------------------------------------------------------------------------
** Copyright 1998-2005 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
** These are different statnums for thinkers. The idea to maintain multiple
** lists for different types of thinkers is taken from Build. Every thinker
** is ticked by statnum, so a thinker with a low statnum will always tick
** before a thinker with a high statnum. If a thinker is not explicitly
** created with a statnum, it will be given MAX_STATNUM.
*/

enum
{ // Thinkers that don't actually think
	STAT_INFO,								// An info queue
	STAT_DECAL,								// A decal
	STAT_CORPSEPOINTER,						// An entry in Hexen's corpse queue
	STAT_TRAVELLING,						// An actor temporarily travelling to a new map

  // Thinkers that do think
	STAT_FIRST_THINKING=32,
	STAT_SCROLLER=STAT_FIRST_THINKING,		// A DScroller thinker
	STAT_PLAYER,							// A player actor
	STAT_BOSSTARGET,						// A boss brain target
	STAT_LIGHTNING,							// The lightning thinker
	STAT_DECALTHINKER,						// An object that thinks for a decal
	STAT_INVENTORY,							// An inventory item
};
